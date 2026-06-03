#include "netfileserver.h"
#include "debug_tx.h"

#include <circle/net/httpdaemon.h>
#include <circle/net/tftpdaemon.h>
#include <circle/bcmwatchdog.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern "C" void MiniJV880_ShowKernelRebootMessage(void);

extern "C" const char* MiniJV880_GetCardRamActivePath(void);
extern "C" const char* MiniJV880_GetCardRamTmpPath(void);
extern "C" int MiniJV880_GetCardRamUsingCollection(void);
extern "C" const char* MiniJV880_GetCardRamCollectionDir(void);
extern "C" const char* MiniJV880_GetCardRamCurrentPath(void);
extern "C" const char* MiniJV880_GetCardRamLegacyPath(void);
extern "C" void MiniJV880_FlushCardRamIfNeededNow(void);

namespace
{
	static const unsigned NETFILE_HTTP_MAX_CONTENT = 8192;

	static const char *kKernelActivePath = "SD:/kernel8-rpi4.img";
	static const char *kKernelStagePath  = "SD:/kernel8-rpi4.img.new";
	static const char *kKernelBackupPath = "SD:/kernel8-rpi4.img.bak";

	static const char *kINIActivePath = "SD:/minijv880.ini";
	static const char *kINIStagePath  = "SD:/minijv880.ini.new";
	static const char *kINIBackupPath = "SD:/minijv880.ini.bak";

	// Dualboot detection paths.
	// Safety rule: MiniDexed is detected read-only only. Kernel staging,
	// activation, backup and delete must manage the MiniJV880 kernel only.
	static const char *kDualbootMiniJV880ActivePath = "SD:/minijv880/kernel8-rpi4.img";
	static const char *kDualbootMiniJV880StagePath  = "SD:/minijv880/kernel8-rpi4.img.new";
	static const char *kDualbootMiniJV880BackupPath = "SD:/minijv880/kernel8-rpi4.img.bak";
	static const char *kDualbootMiniDexedKernelPath = "SD:/minidexed/kernel8-rpi4.img";


	struct TBootLayoutInfo
	{
		const char *LayoutName;
		const char *ManagedKernelActivePath;
		const char *ManagedKernelStagePath;
		const char *ManagedKernelBackupPath;
		bool SinglebootKernelPresent;
		bool DualbootMiniJV880KernelPresent;
		bool MiniDexedKernelPresent;
	};

	static bool FileExistsForBootLayoutDetection (const char *pPath)
	{
		if (pPath == 0 || pPath[0] == '\0')
		{
			return false;
		}

		struct stat StatBuf;
		return stat (pPath, &StatBuf) == 0;
	}

	static void DetectBootLayout (TBootLayoutInfo *pInfo)
	{
		if (pInfo == 0)
		{
			return;
		}

		pInfo->SinglebootKernelPresent =
			FileExistsForBootLayoutDetection (kKernelActivePath);
		pInfo->DualbootMiniJV880KernelPresent =
			FileExistsForBootLayoutDetection (kDualbootMiniJV880ActivePath);
		pInfo->MiniDexedKernelPresent =
			FileExistsForBootLayoutDetection (kDualbootMiniDexedKernelPath);

		// Default/recovery choice keeps the legacy MiniJV880 root-kernel paths.
		pInfo->LayoutName = "unknown";
		pInfo->ManagedKernelActivePath = kKernelActivePath;
		pInfo->ManagedKernelStagePath = kKernelStagePath;
		pInfo->ManagedKernelBackupPath = kKernelBackupPath;

		// Prefer the validated dualboot layout only when both kernels are present.
		// MiniDexed remains read-only/detected-only and is never returned as a
		// managed kernel path.
		if (pInfo->DualbootMiniJV880KernelPresent && pInfo->MiniDexedKernelPresent)
		{
			pInfo->LayoutName = "dualboot";
			pInfo->ManagedKernelActivePath = kDualbootMiniJV880ActivePath;
			pInfo->ManagedKernelStagePath = kDualbootMiniJV880StagePath;
			pInfo->ManagedKernelBackupPath = kDualbootMiniJV880BackupPath;
		}
		else if (pInfo->SinglebootKernelPresent)
		{
			pInfo->LayoutName = "singleboot";
		}
		else if (pInfo->DualbootMiniJV880KernelPresent)
		{
			pInfo->LayoutName = "minijv880-folder";
			pInfo->ManagedKernelActivePath = kDualbootMiniJV880ActivePath;
			pInfo->ManagedKernelStagePath = kDualbootMiniJV880StagePath;
			pInfo->ManagedKernelBackupPath = kDualbootMiniJV880BackupPath;
		}
	}

	static const char *BoolText (bool bValue)
	{
		return bValue
		    ? "<span style=\"color:#0047CC; font-weight:bold;\">yes</span>"
		    : "<span style=\"color:#C00000; font-weight:bold;\">no</span>";
	}

	static const char *KernelStatusHTML (const char *pText)
	{
		if (pText == 0)
		{
			return "";
		}

		if (strcmp (pText, "(missing)") == 0)
		{
			return "<span style=\"color:#C00000; font-weight:bold;\">(missing)</span>";
		}

		if (strcmp (pText, "(read error)") == 0)
		{
			return "<span style=\"color:#C00000; font-weight:bold;\">(read error)</span>";
		}

		if (strcmp (pText, "(close error)") == 0)
		{
			return "<span style=\"color:#C00000; font-weight:bold;\">(close error)</span>";
		}

		return pText;
	}

	static const char *KernelStatusInnerHTML (const char *pText)
	{
		if (pText == 0)
		{
			return "";
		}

		if (strcmp (pText, "(missing)") == 0)
		{
			return "<span style=\"color:#C00000; font-weight:bold;\">missing</span>";
		}

		if (strcmp (pText, "(read error)") == 0)
		{
			return "<span style=\"color:#C00000; font-weight:bold;\">read error</span>";
		}

		if (strcmp (pText, "(close error)") == 0)
		{
			return "<span style=\"color:#C00000; font-weight:bold;\">close error</span>";
		}

		return pText;
	}

	static const char *KernelCommonFooterHTML (void)
	{
		return "<p><a href=\"/kernel-status\">Back to kernel status</a></p>"
		       "<p><a href=\"/status\">Back to status</a></p>"
		       "<p><a href=\"/\">Back to home</a></p>";
	}

	static const char *KernelOpenStatusFooterHTML (void)
	{
		return "<p><a href=\"/kernel-status\">Open kernel status page</a></p>"
		       "<p><a href=\"/status\">Back to status</a></p>"
		       "<p><a href=\"/\">Back to home</a></p>";
	}

	static const char *KernelActivateFooterHTML (void)
	{
		return "<p><a href=\"/kernel-activate\">Back to kernel activate</a></p>"
		       "<p><a href=\"/kernel-status\">Back to kernel status</a></p>"
		       "<p><a href=\"/status\">Back to status</a></p>"
		       "<p><a href=\"/\">Back to home</a></p>";
	}

	static const char *KernelStatusActivateFooterHTML (void)
	{
		return "<p><a href=\"/kernel-status\">Back to kernel status</a></p>"
		       "<p><a href=\"/kernel-activate\">Back to kernel activate</a></p>"
		       "<p><a href=\"/status\">Back to status</a></p>"
		       "<p><a href=\"/\">Back to home</a></p>";
	}

	static const char *KernelOpenStatusActivateFooterHTML (void)
	{
		return "<p><a href=\"/kernel-status\">Open kernel status page</a></p>"
		       "<p><a href=\"/kernel-activate\">Back to kernel activate</a></p>"
		       "<p><a href=\"/status\">Back to status</a></p>"
		       "<p><a href=\"/\">Back to home</a></p>";
	}

	static bool HasSlash (const char *pText)
	{
		return pText != 0 && (strchr (pText, '/') != 0 || strchr (pText, '\\') != 0);
	}

	static bool IsDotName (const char *pText)
	{
		return pText != 0
		    && (strcmp (pText, ".") == 0 || strcmp (pText, "..") == 0);
	}
	
	static bool IsRolandPNFolder (const char *pText)
	{
		return pText != 0 && strcmp (pText, "Roland-PN") == 0;
	}

	static const char *ActionLinkStyle ()
	{
		return "style=\"display:inline-block; border:2px solid #000000; padding:4px 8px; font-weight:bold; color:#C00000; text-decoration:none;\"";
	}

	static bool IsCardRamBINFileName (const char *pFileName)
	{
		if (pFileName == 0 || pFileName[0] == '\0')
		{
			return false;
		}

		if (HasSlash (pFileName) || IsDotName (pFileName))
		{
			return false;
		}

		const char *pExt = strrchr (pFileName, '.');
		if (pExt == 0)
		{
			return false;
		}

		return (pExt[1] == 'b' || pExt[1] == 'B')
		    && (pExt[2] == 'i' || pExt[2] == 'I')
		    && (pExt[3] == 'n' || pExt[3] == 'N')
		    && pExt[4] == '\0';
	}

	static bool WriteCardRamCurrentSelectionFile (const char *pCardName)
	{
		if (!IsCardRamBINFileName (pCardName))
		{
			return false;
		}

		static const char *pFinalPath = "SD:/CARD-RAM/current.txt";
		static const char *pTempPath  = "SD:/CARD-RAM/current.txt.tmp";

		FILE *pOutput = fopen (pTempPath, "wb");
		if (pOutput == 0)
		{
			return false;
		}

		bool bOK = true;

		size_t nNameLen = strlen (pCardName);
		if (fwrite (pCardName, 1, nNameLen, pOutput) != nNameLen)
		{
			bOK = false;
		}

		if (bOK && fwrite ("\n", 1, 1, pOutput) != 1)
		{
			bOK = false;
		}

		if (fclose (pOutput) != 0)
		{
			bOK = false;
		}

		if (!bOK)
		{
			remove (pTempPath);
			return false;
		}

		remove (pFinalPath);

		if (rename (pTempPath, pFinalPath) != 0)
		{
			remove (pTempPath);
			return false;
		}

		return true;
	}

	struct TTFTPSYXUploadPath
	{
		char FolderName[256];
		char FileName[256];
		char FinalPath[768];
		char TempPath[768];
	};

	static bool IsSYXFileName (const char *pFileName)
	{
		if (pFileName == 0)
		{
			return false;
		}

		const char *pExt = strrchr (pFileName, '.');
		if (pExt == 0)
		{
			return false;
		}

		return (pExt[1] == 's' || pExt[1] == 'S')
		    && (pExt[2] == 'y' || pExt[2] == 'Y')
		    && (pExt[3] == 'x' || pExt[3] == 'X')
		    && pExt[4] == '\0';
	}

	static bool ParseTFTPSYXUploadPath (
		const char *pFileName,
		TTFTPSYXUploadPath *pPath)
	{
		if (pFileName == 0 || pPath == 0)
		{
			return false;
		}

		memset (pPath, 0, sizeof *pPath);

		if (strchr (pFileName, '\\') != 0)
		{
			return false;
		}

		const char *pSlash = strchr (pFileName, '/');
		if (pSlash == 0 || pSlash == pFileName)
		{
			return false;
		}

		if (strchr (pSlash + 1, '/') != 0)
		{
			return false;
		}

		if (pSlash[1] == '\0')
		{
			return false;
		}

		size_t nFolderLen = (size_t) (pSlash - pFileName);
		size_t nFileLen = strlen (pSlash + 1);

		if (nFolderLen >= sizeof pPath->FolderName
			|| nFileLen >= sizeof pPath->FileName)
		{
			return false;
		}

		memcpy (pPath->FolderName, pFileName, nFolderLen);
		pPath->FolderName[nFolderLen] = '\0';

		memcpy (pPath->FileName, pSlash + 1, nFileLen);
		pPath->FileName[nFileLen] = '\0';

		if (pPath->FolderName[0] == '\0'
			|| pPath->FileName[0] == '\0'
			|| IsDotName (pPath->FolderName)
			|| IsDotName (pPath->FileName)
			|| IsRolandPNFolder (pPath->FolderName)
			|| !IsSYXFileName (pPath->FileName))
		{
			return false;
		}

		int nFinalWritten = snprintf (
			pPath->FinalPath, sizeof pPath->FinalPath,
			"SD:/PN-JV80/%s/%s",
			pPath->FolderName,
			pPath->FileName);

		if (nFinalWritten < 0 || (size_t) nFinalWritten >= sizeof pPath->FinalPath)
		{
			return false;
		}

		int nTempWritten = snprintf (
			pPath->TempPath, sizeof pPath->TempPath,
			"%s.tmp",
			pPath->FinalPath);

		if (nTempWritten < 0 || (size_t) nTempWritten >= sizeof pPath->TempPath)
		{
			pPath->FinalPath[0] = '\0';
			return false;
		}

		return true;
	}
	
	struct TTFTPCardRAMUploadPath
	{
		char FileName[256];
		char FinalPath[768];
		char TempPath[768];
	};

	static bool IsBINFileName (const char *pFileName)
	{
		if (pFileName == 0)
		{
			return false;
		}

		const char *pExt = strrchr (pFileName, '.');
		if (pExt == 0 || pExt == pFileName)
		{
			return false;
		}

		return (pExt[1] == 'b' || pExt[1] == 'B')
		    && (pExt[2] == 'i' || pExt[2] == 'I')
		    && (pExt[3] == 'n' || pExt[3] == 'N')
		    && pExt[4] == '\0';
	}

	static bool ParseTFTPCardRAMUploadPath (
		const char *pFileName,
		TTFTPCardRAMUploadPath *pPath)
	{
		if (pFileName == 0 || pPath == 0)
		{
			return false;
		}

		memset (pPath, 0, sizeof *pPath);

		if (strchr (pFileName, '\\') != 0)
		{
			return false;
		}

		const char *pSlash = strchr (pFileName, '/');
		if (pSlash == 0 || pSlash == pFileName)
		{
			return false;
		}

		if (strchr (pSlash + 1, '/') != 0)
		{
			return false;
		}

		if (pSlash[1] == '\0')
		{
			return false;
		}

		size_t nFolderLen = (size_t) (pSlash - pFileName);
		size_t nFileLen = strlen (pSlash + 1);

		char FolderName[32];
		if (nFolderLen >= sizeof FolderName
			|| nFileLen >= sizeof pPath->FileName)
		{
			return false;
		}

		memcpy (FolderName, pFileName, nFolderLen);
		FolderName[nFolderLen] = '\0';

		memcpy (pPath->FileName, pSlash + 1, nFileLen);
		pPath->FileName[nFileLen] = '\0';

		if (FolderName[0] == '\0'
			|| pPath->FileName[0] == '\0'
			|| IsDotName (FolderName)
			|| IsDotName (pPath->FileName)
			|| strcmp (FolderName, "CARD-RAM") != 0
			|| !IsBINFileName (pPath->FileName))
		{
			return false;
		}

		int nFinalWritten = snprintf (
			pPath->FinalPath, sizeof pPath->FinalPath,
			"SD:/CARD-RAM/%s",
			pPath->FileName);

		if (nFinalWritten < 0 || (size_t) nFinalWritten >= sizeof pPath->FinalPath)
		{
			return false;
		}

		int nTempWritten = snprintf (
			pPath->TempPath, sizeof pPath->TempPath,
			"%s.tmp",
			pPath->FinalPath);

		if (nTempWritten < 0 || (size_t) nTempWritten >= sizeof pPath->TempPath)
		{
			pPath->FinalPath[0] = '\0';
			return false;
		}

		return true;
	}

	static int HexValue (char ch)
	{
		if (ch >= '0' && ch <= '9')
		{
			return ch - '0';
		}

		if (ch >= 'A' && ch <= 'F')
		{
			return 10 + (ch - 'A');
		}

		if (ch >= 'a' && ch <= 'f')
		{
			return 10 + (ch - 'a');
		}

		return -1;
	}

	static bool URLDecode (const char *pSrc, char *pDst, size_t nDstSize)
	{
		if (pSrc == 0 || pDst == 0 || nDstSize == 0)
		{
			return false;
		}

		size_t nOut = 0;

		for (size_t i = 0; pSrc[i] != '\0'; i++)
		{
			unsigned char ch = (unsigned char) pSrc[i];

			if (ch == '%')
			{
				int hi = HexValue (pSrc[i + 1]);
				int lo = HexValue (pSrc[i + 2]);

				if (hi < 0 || lo < 0)
				{
					return false;
				}

				ch = (unsigned char) ((hi << 4) | lo);
				i += 2;
			}

			if (nOut + 1 >= nDstSize)
			{
				return false;
			}

			pDst[nOut++] = (char) ch;
		}

		pDst[nOut] = '\0';
		return true;
	}
	
	static int CompareNameRows (const void *pA, const void *pB)
	{
	    return strcmp ((const char *) pA, (const char *) pB);
	}

	static bool FindNextCardRamBINName (
		const char *pAfterName,
		char *pNextName,
		size_t nNextNameSize,
		bool *pbFound)
	{
		if (pNextName == 0 || nNextNameSize == 0 || pbFound == 0)
		{
			return false;
		}

		*pbFound = false;
		pNextName[0] = '\0';

		DIR *pDir = opendir ("SD:/CARD-RAM");
		if (pDir == 0)
		{
			return false;
		}

		for (;;)
		{
			struct dirent *pEntry = readdir (pDir);
			if (pEntry == 0)
			{
				break;
			}

			if (!IsCardRamBINFileName (pEntry->d_name))
			{
				continue;
			}

			if (pAfterName != 0
				&& pAfterName[0] != '\0'
				&& strcmp (pEntry->d_name, pAfterName) <= 0)
			{
				continue;
			}

			if (!*pbFound || strcmp (pEntry->d_name, pNextName) < 0)
			{
				int nWritten = snprintf (
					pNextName, nNextNameSize,
					"%s",
					pEntry->d_name);

				if (nWritten < 0 || (size_t) nWritten >= nNextNameSize)
				{
					closedir (pDir);
					return false;
				}

				*pbFound = true;
			}
		}

		closedir (pDir);
		return true;
	}
	
	static char HexDigit (unsigned value)
	{
		return value < 10 ? (char) ('0' + value) : (char) ('A' + (value - 10));
	}

	static bool URLEncodePathSegment (const char *pSrc, char *pDst, size_t nDstSize)
	{
		if (pSrc == 0 || pDst == 0 || nDstSize == 0)
		{
			return false;
		}

		size_t nOut = 0;

		for (size_t i = 0; pSrc[i] != '\0'; i++)
		{
			unsigned char ch = (unsigned char) pSrc[i];
			bool bSafe =
				   (ch >= 'A' && ch <= 'Z')
				|| (ch >= 'a' && ch <= 'z')
				|| (ch >= '0' && ch <= '9')
				|| ch == '-'
				|| ch == '_'
				|| ch == '.'
				|| ch == '~';

			if (bSafe)
			{
				if (nOut + 1 >= nDstSize)
				{
					return false;
				}

				pDst[nOut++] = (char) ch;
			}
			else
			{
				if (nOut + 3 >= nDstSize)
				{
					return false;
				}

				pDst[nOut++] = '%';
				pDst[nOut++] = HexDigit ((ch >> 4) & 0x0F);
				pDst[nOut++] = HexDigit (ch & 0x0F);
			}
		}

		pDst[nOut] = '\0';
		return true;
	}

	    static bool CopyFileContents (const char *pSourcePath, const char *pTargetPath)
	    {
		if (pSourcePath == 0 || pTargetPath == 0)
		{
			return false;
		}

		FILE *pInput = fopen (pSourcePath, "rb");
		if (pInput == 0)
		{
			return false;
		}

		FILE *pOutput = fopen (pTargetPath, "wb");
		if (pOutput == 0)
		{
			fclose (pInput);
			return false;
		}

		bool bOK = true;
		unsigned char Buffer[1024];

		for (;;)
		{
			size_t nRead = fread (Buffer, 1, sizeof Buffer, pInput);

			if (nRead > 0)
			{
				size_t nWritten = fwrite (Buffer, 1, nRead, pOutput);
				if (nWritten != nRead)
				{
					bOK = false;
					break;
				}
			}

			if (nRead < sizeof Buffer)
			{
				if (ferror (pInput))
				{
					bOK = false;
				}
				break;
			}
		}

		if (fclose (pOutput) != 0)
		{
			bOK = false;
		}

		if (fclose (pInput) != 0)
		{
			bOK = false;
		}

		if (!bOK)
		{
			remove (pTargetPath);
		}

		return bOK;
	}

	static bool GetKernelFileStatusText (
		const char *pPath,
		bool *pbExists,
		char *pSizeText,
		size_t nSizeTextSize)
	{
		if (pPath == 0 || pbExists == 0 || pSizeText == 0 || nSizeTextSize == 0)
		{
			return false;
		}

		struct stat StatBuf;
		if (stat (pPath, &StatBuf) != 0)
		{
			*pbExists = false;

			int nWritten = snprintf (pSizeText, nSizeTextSize, "(missing)");
			if (nWritten < 0 || (size_t) nWritten >= nSizeTextSize)
			{
				return false;
			}

			return true;
		}

		*pbExists = true;

		int nWritten = snprintf (
			pSizeText, nSizeTextSize,
			"%ld bytes",
			(long) StatBuf.st_size);

		if (nWritten < 0 || (size_t) nWritten >= nSizeTextSize)
		{
			return false;
		}

		return true;
	}

	static bool GetKernelFileDigestText (
		const char *pPath,
		char *pDigestText,
		size_t nDigestTextSize)
	{
		if (pPath == 0 || pDigestText == 0 || nDigestTextSize == 0)
		{
			return false;
		}

		FILE *pInput = fopen (pPath, "rb");
		if (pInput == 0)
		{
			int nWritten = snprintf (pDigestText, nDigestTextSize, "(missing)");
			return nWritten >= 0 && (size_t) nWritten < nDigestTextSize;
		}

		unsigned Digest = 2166136261u;
		unsigned char Buffer[128];

		for (;;)
		{
			size_t nRead = fread (Buffer, 1, sizeof Buffer, pInput);

			for (size_t i = 0; i < nRead; i++)
			{
				Digest ^= (unsigned) Buffer[i];
				Digest *= 16777619u;
			}

			if (nRead < sizeof Buffer)
			{
				if (ferror (pInput))
				{
					fclose (pInput);
					int nWritten = snprintf (pDigestText, nDigestTextSize, "(read error)");
					return nWritten >= 0 && (size_t) nWritten < nDigestTextSize;
				}
				break;
			}
		}

		if (fclose (pInput) != 0)
		{
			int nWritten = snprintf (pDigestText, nDigestTextSize, "(close error)");
			return nWritten >= 0 && (size_t) nWritten < nDigestTextSize;
		}

		int nWritten = snprintf (
			pDigestText, nDigestTextSize,
			"0x%08lX",
			(unsigned long) Digest);

		return nWritten >= 0 && (size_t) nWritten < nDigestTextSize;
	}

	static bool GetFirstDifferingLine (
		const char *pLeftPath,
		const char *pRightPath,
		bool *pbIdentical,
		unsigned *pnFirstDifferingLine)
	{
		if (pLeftPath == 0 || pRightPath == 0 || pbIdentical == 0 || pnFirstDifferingLine == 0)
		{
			return false;
		}

		FILE *pLeft = fopen (pLeftPath, "rb");
		if (pLeft == 0)
		{
			return false;
		}

		FILE *pRight = fopen (pRightPath, "rb");
		if (pRight == 0)
		{
			fclose (pLeft);
			return false;
		}

		bool bIdentical = true;
		unsigned nLine = 1;

		for (;;)
		{
			int nLeft = fgetc (pLeft);
			int nRight = fgetc (pRight);

			if (nLeft != nRight)
			{
				bIdentical = false;
				*pnFirstDifferingLine = nLine;
				break;
			}

			if (nLeft == EOF)
			{
				*pnFirstDifferingLine = 0;
				break;
			}

			if (nLeft == '\n')
			{
				nLine++;
			}
		}

		bool bOK = !ferror (pLeft) && !ferror (pRight);

		if (fclose (pLeft) != 0 || fclose (pRight) != 0)
		{
			bOK = false;
		}

		if (!bOK)
		{
			return false;
		}

		*pbIdentical = bIdentical;
		if (bIdentical)
		{
			*pnFirstDifferingLine = 0;
		}

		return true;
	}

	struct TCopyMovePageScratch
	{
		char ItemPath[512];
		char ItemName[512];
		char FullPath[768];
		char SizeText[64];
		char EncodedParent[768];
		char EncodedChild[768];
	};

	struct TUploadTextProbeScratch
	{
		char FolderName[256];
		char FullPath[768];
		char EncodedFolder[768];
		char FileName[256];
		char FirstBytesText[64];
	};

	struct TUploadTextSaveScratch
	{
		char FolderName[256];
		char FullPath[768];
		char EncodedFolder[768];
		char FileName[256];
		char EncodedFile[768];
		char FinalPath[768];
		char TempPath[768];
	};

	struct TDestinationFolderListScratch
	{
		char FolderNames[24][256];
	};

	struct TSiblingFolderListScratch
	{
		char FolderNames[64][256];
	};

	static bool AppendDestinationFolderList (
		char *pPage,
		size_t nPageSize,
		size_t *pnUsed,
		const char *pExcludeFolder)
	{
		if (pPage == 0 || pnUsed == 0 || *pnUsed >= nPageSize)
		{
			return false;
		}

		TDestinationFolderListScratch *pScratch =
			(TDestinationFolderListScratch *) malloc (sizeof (TDestinationFolderListScratch));
		if (pScratch == 0)
		{
			return false;
		}

		bool bOK = false;
		unsigned nFolderCount = 0;
		bool bTruncated = false;

		DIR *pFolderDir = opendir ("SD:/PN-JV80");
		if (pFolderDir != 0)
		{
			struct dirent *pEntry;

			while ((pEntry = readdir (pFolderDir)) != 0)
			{
				const char *pName = pEntry->d_name;
				if (pName == 0 || pName[0] == '\0' || IsDotName (pName))
				{
					continue;
				}

				if (IsRolandPNFolder (pName))
				{
					continue;
				}

				if (pExcludeFolder != 0 && strcmp (pName, pExcludeFolder) == 0)
				{
					continue;
				}

				char CandidatePath[768];
				int nCandidateWritten = snprintf (
					CandidatePath, sizeof CandidatePath,
					"SD:/PN-JV80/%s", pName);

				if (nCandidateWritten < 0 || (unsigned) nCandidateWritten >= sizeof CandidatePath)
				{
					continue;
				}

				DIR *pCandidateDir = opendir (CandidatePath);
				if (pCandidateDir == 0)
				{
					continue;
				}
				closedir (pCandidateDir);

				if (nFolderCount < 24)
				{
					int nNameWritten = snprintf (
						pScratch->FolderNames[nFolderCount],
						sizeof pScratch->FolderNames[nFolderCount],
						"%s",
						pName);

					if (nNameWritten < 0
						|| (unsigned) nNameWritten >= sizeof pScratch->FolderNames[nFolderCount])
					{
						closedir (pFolderDir);
						free (pScratch);
						return false;
					}

					nFolderCount++;
				}
				else
				{
					bTruncated = true;
				}
			}

			closedir (pFolderDir);
		}

		if (nFolderCount > 1)
		{
			qsort (pScratch->FolderNames, nFolderCount, sizeof pScratch->FolderNames[0], CompareNameRows);
		}

		size_t nUsed = *pnUsed;
		int nWritten;

		if (nFolderCount != 0)
		{
			nWritten = snprintf (
				pPage + nUsed, nPageSize - nUsed,
				"<h2>Available destination subfolders</h2>"
				"<p>Type one of these names into the destination field above.</p>"
				"<ul>");

			if (nWritten < 0 || (size_t) nWritten >= nPageSize - nUsed)
			{
				free (pScratch);
				return false;
			}

			nUsed += (size_t) nWritten;

			for (unsigned i = 0; i < nFolderCount; i++)
			{
				nWritten = snprintf (
					pPage + nUsed, nPageSize - nUsed,
					"<li>%s</li>",
					pScratch->FolderNames[i]);

				if (nWritten < 0 || (size_t) nWritten >= nPageSize - nUsed)
				{
					free (pScratch);
					return false;
				}

				nUsed += (size_t) nWritten;
			}

			if (bTruncated)
			{
				nWritten = snprintf (
					pPage + nUsed, nPageSize - nUsed,
					"<li>(listing truncated)</li>");

				if (nWritten < 0 || (size_t) nWritten >= nPageSize - nUsed)
				{
					free (pScratch);
					return false;
				}

				nUsed += (size_t) nWritten;
			}

			nWritten = snprintf (
				pPage + nUsed, nPageSize - nUsed,
				"</ul>");

			if (nWritten < 0 || (size_t) nWritten >= nPageSize - nUsed)
			{
				free (pScratch);
				return false;
			}

			nUsed += (size_t) nWritten;
		}
		else
		{
			nWritten = snprintf (
				pPage + nUsed, nPageSize - nUsed,
				"<p>No destination subfolders are currently available.</p>");

			if (nWritten < 0 || (size_t) nWritten >= nPageSize - nUsed)
			{
				free (pScratch);
				return false;
			}

			nUsed += (size_t) nWritten;
		}

		*pnUsed = nUsed;
		bOK = true;
		free (pScratch);
		return bOK;
	}

	    static THTTPStatus HandleCopyPage (
		const TNetFileServerConfig& Config,
		const char *pPath,
		const char *pParams,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	    {
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		(void) pParams;

		THTTPStatus Result = HTTPInternalServerError;
		TCopyMovePageScratch *pScratch = (TCopyMovePageScratch *) malloc (sizeof (TCopyMovePageScratch));
		FILE *pInput = 0;

		if (pScratch == 0)
		{
			return HTTPInternalServerError;
		}

		do
		{
			const char *pEncodedName = pPath + 14;   // "/copy/PN-JV80/"
			if (pEncodedName[0] == '\0')
			{
				Result = HTTPNotFound;
				break;
			}

			if (!URLDecode (pEncodedName, pScratch->ItemPath, sizeof pScratch->ItemPath))
			{
				Result = HTTPNotFound;
				break;
			}

			if (pScratch->ItemPath[0] == '\0' || strchr (pScratch->ItemPath, '\\') != 0)
			{
				Result = HTTPNotFound;
				break;
			}

			unsigned nSlashCount = 0;
			for (const char *pScan = pScratch->ItemPath; *pScan != '\0'; pScan++)
			{
				if (*pScan == '/')
				{
					nSlashCount++;
				}
			}

			if (nSlashCount != 1)
			{
				Result = HTTPNotFound;
				break;
			}

			int nItemWritten = snprintf (
				pScratch->ItemName, sizeof pScratch->ItemName,
				"%s", pScratch->ItemPath);

			if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof pScratch->ItemName)
			{
				Result = HTTPInternalServerError;
				break;
			}

			char *pSlash = strchr (pScratch->ItemPath, '/');
			if (pSlash == 0)
			{
				Result = HTTPNotFound;
				break;
			}

			*pSlash = '\0';

			const char *pParentName = pScratch->ItemPath;
			const char *pChildName = pSlash + 1;

			if (pParentName[0] == '\0' || pChildName[0] == '\0')
			{
				Result = HTTPNotFound;
				break;
			}

			if (IsDotName (pParentName) || IsDotName (pChildName))
			{
				Result = HTTPNotFound;
				break;
			}

			const char *pExt = strrchr (pChildName, '.');
			if (pExt == 0
				|| !((pExt[1] == 's' || pExt[1] == 'S')
				  && (pExt[2] == 'y' || pExt[2] == 'Y')
				  && (pExt[3] == 'x' || pExt[3] == 'X')
				  && pExt[4] == '\0'))
			{
				Result = HTTPNotFound;
				break;
			}

			int nPathWritten = snprintf (
				pScratch->FullPath, sizeof pScratch->FullPath,
				"SD:/PN-JV80/%s", pScratch->ItemName);

			if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof pScratch->FullPath)
			{
				Result = HTTPInternalServerError;
				break;
			}

			DIR *pTestDir = opendir (pScratch->FullPath);
			if (pTestDir != 0)
			{
				closedir (pTestDir);
				Result = HTTPNotFound;
				break;
			}

			pInput = fopen (pScratch->FullPath, "rb");
			if (pInput == 0)
			{
				Result = HTTPNotFound;
				break;
			}

			if (fseek (pInput, 0, SEEK_END) == 0)
			{
				long nFileSize = ftell (pInput);
				if (nFileSize >= 0)
				{
					snprintf (pScratch->SizeText, sizeof pScratch->SizeText, "%ld bytes", nFileSize);
				}
				else
				{
					snprintf (pScratch->SizeText, sizeof pScratch->SizeText, "unknown");
				}
			}
			else
			{
				snprintf (pScratch->SizeText, sizeof pScratch->SizeText, "unknown");
			}

			fclose (pInput);
			pInput = 0;

			if (!URLEncodePathSegment (pParentName, pScratch->EncodedParent, sizeof pScratch->EncodedParent)
				|| !URLEncodePathSegment (pChildName, pScratch->EncodedChild, sizeof pScratch->EncodedChild))
			{
				Result = HTTPInternalServerError;
				break;
			}

			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Copy: PN-JV80/%s</title></head>"
				"<body>"
				"<h1>Copy: PN-JV80/%s</h1>"
				"<p>Enter the destination subfolder and submit to execute the copy. No file has been modified yet.</p>"
				"<ul>"
				"<li>Current file name: %s</li>"
				"<li>Current folder: %s</li>"
				"<li>SD path: %s</li>"
				"<li>File size: %s</li>"
				"</ul>"
				"<form method=\"get\" action=\"/copy-exec/PN-JV80/%s/%s\">"
				"<p>Destination subfolder: <input type=\"text\" name=\"dest\" size=\"40\"></p>"
				"<p><input type=\"submit\" value=\"Copy now\"></p>"
				"</form>",
				pScratch->ItemName,
				pScratch->ItemName,
				pChildName,
				pParentName,
				pScratch->FullPath,
				pScratch->SizeText,
				pScratch->EncodedParent,
				pScratch->EncodedChild);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			size_t nUsed = (size_t) nWritten;

			if (!AppendDestinationFolderList (PNPage, nPNPageSize, &nUsed, pParentName))
			{
				Result = HTTPInternalServerError;
				break;
			}

			nWritten = snprintf (
				PNPage + nUsed, nPNPageSize - nUsed,
				"<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->EncodedParent,
				pScratch->EncodedChild,
				pScratch->EncodedParent);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize - nUsed)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
		}
		while (0);

		if (pInput != 0)
		{
			fclose (pInput);
		}

		free (pScratch);
		return Result;
	    }	

	static THTTPStatus HandleMovePage (
		const TNetFileServerConfig& Config,
		const char *pPath,
		const char *pParams,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		(void) pParams;

		THTTPStatus Result = HTTPInternalServerError;
		TCopyMovePageScratch *pScratch = (TCopyMovePageScratch *) malloc (sizeof (TCopyMovePageScratch));
		FILE *pInput = 0;

		if (pScratch == 0)
		{
			return HTTPInternalServerError;
		}

		do
		{
			const char *pEncodedName = pPath + 14;   // "/move/PN-JV80/"
			if (pEncodedName[0] == '\0')
			{
				Result = HTTPNotFound;
				break;
			}

			if (!URLDecode (pEncodedName, pScratch->ItemPath, sizeof pScratch->ItemPath))
			{
				Result = HTTPNotFound;
				break;
			}

			if (pScratch->ItemPath[0] == '\0' || strchr (pScratch->ItemPath, '\\') != 0)
			{
				Result = HTTPNotFound;
				break;
			}

			unsigned nSlashCount = 0;
			for (const char *pScan = pScratch->ItemPath; *pScan != '\0'; pScan++)
			{
				if (*pScan == '/')
				{
					nSlashCount++;
				}
			}

			if (nSlashCount != 1)
			{
				Result = HTTPNotFound;
				break;
			}

			int nItemWritten = snprintf (
				pScratch->ItemName, sizeof pScratch->ItemName,
				"%s", pScratch->ItemPath);

			if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof pScratch->ItemName)
			{
				Result = HTTPInternalServerError;
				break;
			}

			char *pSlash = strchr (pScratch->ItemPath, '/');
			if (pSlash == 0)
			{
				Result = HTTPNotFound;
				break;
			}

			*pSlash = '\0';

			const char *pParentName = pScratch->ItemPath;
			const char *pChildName = pSlash + 1;

			if (pParentName[0] == '\0' || pChildName[0] == '\0')
			{
				Result = HTTPNotFound;
				break;
			}

			if (IsDotName (pParentName) || IsDotName (pChildName))
			{
				Result = HTTPNotFound;
				break;
			}

			const char *pExt = strrchr (pChildName, '.');
			if (pExt == 0
				|| !((pExt[1] == 's' || pExt[1] == 'S')
				  && (pExt[2] == 'y' || pExt[2] == 'Y')
				  && (pExt[3] == 'x' || pExt[3] == 'X')
				  && pExt[4] == '\0'))
			{
				Result = HTTPNotFound;
				break;
			}

			int nPathWritten = snprintf (
				pScratch->FullPath, sizeof pScratch->FullPath,
				"SD:/PN-JV80/%s", pScratch->ItemName);

			if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof pScratch->FullPath)
			{
				Result = HTTPInternalServerError;
				break;
			}

			DIR *pTestDir = opendir (pScratch->FullPath);
			if (pTestDir != 0)
			{
				closedir (pTestDir);
				Result = HTTPNotFound;
				break;
			}

			pInput = fopen (pScratch->FullPath, "rb");
			if (pInput == 0)
			{
				Result = HTTPNotFound;
				break;
			}

			if (fseek (pInput, 0, SEEK_END) == 0)
			{
				long nFileSize = ftell (pInput);
				if (nFileSize >= 0)
				{
					snprintf (pScratch->SizeText, sizeof pScratch->SizeText, "%ld bytes", nFileSize);
				}
				else
				{
					snprintf (pScratch->SizeText, sizeof pScratch->SizeText, "unknown");
				}
			}
			else
			{
				snprintf (pScratch->SizeText, sizeof pScratch->SizeText, "unknown");
			}

			fclose (pInput);
			pInput = 0;

			if (!URLEncodePathSegment (pParentName, pScratch->EncodedParent, sizeof pScratch->EncodedParent)
				|| !URLEncodePathSegment (pChildName, pScratch->EncodedChild, sizeof pScratch->EncodedChild))
			{
				Result = HTTPInternalServerError;
				break;
			}

			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Move: PN-JV80/%s</title></head>"
				"<body>"
				"<h1>Move: PN-JV80/%s</h1>"
				"<p>Enter the destination subfolder and submit to execute the move. No file has been modified yet.</p>"
				"<ul>"
				"<li>Current file name: %s</li>"
				"<li>Current folder: %s</li>"
				"<li>SD path: %s</li>"
				"<li>File size: %s</li>"
				"</ul>"
				"<form method=\"get\" action=\"/move-exec/PN-JV80/%s/%s\">"
				"<p>Destination subfolder: <input type=\"text\" name=\"dest\" size=\"40\"></p>"
				"<p><input type=\"submit\" value=\"Move now\"></p>"
				"</form>",
				pScratch->ItemName,
				pScratch->ItemName,
				pChildName,
				pParentName,
				pScratch->FullPath,
				pScratch->SizeText,
				pScratch->EncodedParent,
				pScratch->EncodedChild);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			size_t nUsed = (size_t) nWritten;

			if (!AppendDestinationFolderList (PNPage, nPNPageSize, &nUsed, pParentName))
			{
				Result = HTTPInternalServerError;
				break;
			}

			nWritten = snprintf (
				PNPage + nUsed, nPNPageSize - nUsed,
				"<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->EncodedParent,
				pScratch->EncodedChild,
				pScratch->EncodedParent);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize - nUsed)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
		}
		while (0);

		if (pInput != 0)
		{
			fclose (pInput);
		}

		free (pScratch);
		return Result;
	}	

	static THTTPStatus HandleCreateFolderPage (
		const TNetFileServerConfig& Config,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Create folder: PN-JV80</title></head>"
			"<body>"
			"<h1>Create folder: PN-JV80</h1>"
			"<p>Enter the name of a new subfolder to create in PN-JV80. No folder has been created yet.</p>"
			"<form method=\"get\" action=\"/dir-create-exec/PN-JV80\">"
			"<p>New folder name: <input type=\"text\" name=\"name\" size=\"40\"></p>"
			"<p><input type=\"submit\" value=\"Create folder\"></p>"
			"</form>"
			"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
			"<p><a href=\"/browse\">Back to browse root</a></p>"
			"<p><a href=\"/\">Back to home</a></p>"
			"</body>"
			"</html>");

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			return HTTPInternalServerError;
		}

		*ppBody = PNPage;
		return HTTPOK;
	}

	static THTTPStatus HandleRenameFolderPage (
		const TNetFileServerConfig& Config,
		const char *pPath,
		const char *pParams,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		(void) pParams;

		const char *pEncodedName = pPath + 20;   // "/dir-rename/PN-JV80/"
		if (pEncodedName[0] == '\0')
		{
			return HTTPNotFound;
		}

		char FolderName[256];
		if (!URLDecode (pEncodedName, FolderName, sizeof FolderName))
		{
			return HTTPNotFound;
		}

		if (FolderName[0] == '\0'
			|| HasSlash (FolderName)
			|| IsDotName (FolderName)
			|| IsRolandPNFolder (FolderName))
		{
			return HTTPNotFound;
		}

		char FullPath[768];
		int nPathWritten = snprintf (
			FullPath, sizeof FullPath,
			"SD:/PN-JV80/%s", FolderName);

		if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
		{
			return HTTPInternalServerError;
		}

		DIR *pDir = opendir (FullPath);
		if (pDir == 0)
		{
			return HTTPNotFound;
		}
		closedir (pDir);

		char EncodedFolder[768];
		if (!URLEncodePathSegment (FolderName, EncodedFolder, sizeof EncodedFolder))
		{
			return HTTPInternalServerError;
		}

		int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Rename folder: PN-JV80/%s</title></head>"
			"<body>"
			"<h1>Rename folder: PN-JV80/%s</h1>"
			"<p>Enter a new name for this subfolder and submit to execute the rename. No folder has been modified yet.</p>"
			"<ul>"
			"<li>Current folder name: %s</li>"
			"<li>SD path: %s</li>"
			"</ul>"
			"<form method=\"get\" action=\"/dir-rename-exec/PN-JV80/%s\">"
			"<p>New folder name: <input type=\"text\" name=\"newname\" size=\"40\"></p>"
			"<p><input type=\"submit\" value=\"Rename folder\"></p>"
			"</form>"
			"<p><a href=\"/dir-siblings/PN-JV80/%s\">Show existing sibling subfolder names</a></p>"
			"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
			"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
			"<p><a href=\"/browse\">Back to browse root</a></p>"
			"<p><a href=\"/\">Back to home</a></p>"
			"</body>"
			"</html>",
			FolderName,
			FolderName,
			FolderName,
			FullPath,
			EncodedFolder,
			EncodedFolder,
			EncodedFolder);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			return HTTPInternalServerError;
		}

		*ppBody = PNPage;
		return HTTPOK;
	}

	static THTTPStatus HandleDeleteFolderPage (
		const TNetFileServerConfig& Config,
		const char *pPath,
		const char *pParams,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		(void) pParams;

		const char *pEncodedName = pPath + 20;   // "/dir-delete/PN-JV80/"
		if (pEncodedName[0] == '\0')
		{
			return HTTPNotFound;
		}

		char FolderName[256];
		if (!URLDecode (pEncodedName, FolderName, sizeof FolderName))
		{
			return HTTPNotFound;
		}

		if (FolderName[0] == '\0'
			|| HasSlash (FolderName)
			|| IsDotName (FolderName)
			|| IsRolandPNFolder (FolderName))
		{
			return HTTPNotFound;
		}

		char FullPath[768];
		int nPathWritten = snprintf (
			FullPath, sizeof FullPath,
			"SD:/PN-JV80/%s", FolderName);

		if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
		{
			return HTTPInternalServerError;
		}

		DIR *pDir = opendir (FullPath);
		if (pDir == 0)
		{
			return HTTPNotFound;
		}
		closedir (pDir);

		char EncodedFolder[768];
		if (!URLEncodePathSegment (FolderName, EncodedFolder, sizeof EncodedFolder))
		{
			return HTTPInternalServerError;
		}

		int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Delete folder: PN-JV80/%s</title></head>"
			"<body>"
			"<h1>Delete folder: PN-JV80/%s</h1>"
			"<p>This operation deletes the subfolder only if it is empty.</p>"
			"<ul>"
			"<li>Folder name: %s</li>"
			"<li>SD path: %s</li>"
			"</ul>"
			"<p><a href=\"/dir-delete-exec/PN-JV80/%s\">Delete this folder now</a></p>"
			"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
			"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
			"<p><a href=\"/browse\">Back to browse root</a></p>"
			"<p><a href=\"/\">Back to home</a></p>"
			"</body>"
			"</html>",
			FolderName,
			FolderName,
			FolderName,
			FullPath,
			EncodedFolder,
			EncodedFolder);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			return HTTPInternalServerError;
		}

		*ppBody = PNPage;
		return HTTPOK;
	}

static THTTPStatus HandleSiblingFolderListPage (
		const TNetFileServerConfig& Config,
		const char *pPath,
		const char *pParams,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		(void) pParams;

		const char *pEncodedName = pPath + 22;   // "/dir-siblings/PN-JV80/"
		if (pEncodedName[0] == '\0')
		{
			return HTTPNotFound;
		}

		char FolderName[256];
		if (!URLDecode (pEncodedName, FolderName, sizeof FolderName))
		{
			return HTTPNotFound;
		}

		if (FolderName[0] == '\0'
			|| HasSlash (FolderName)
			|| IsDotName (FolderName)
			|| IsRolandPNFolder (FolderName))
		{
			return HTTPNotFound;
		}

		char FullPath[768];
		int nPathWritten = snprintf (
			FullPath, sizeof FullPath,
			"SD:/PN-JV80/%s", FolderName);

		if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
		{
			return HTTPInternalServerError;
		}

		DIR *pDir = opendir (FullPath);
		if (pDir == 0)
		{
			return HTTPNotFound;
		}
		closedir (pDir);

		char EncodedFolder[768];
		if (!URLEncodePathSegment (FolderName, EncodedFolder, sizeof EncodedFolder))
		{
			return HTTPInternalServerError;
		}

		int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Sibling subfolders: PN-JV80/%s</title></head>"
			"<body>"
			"<h1>Sibling subfolders: PN-JV80/%s</h1>"
			"<p>The list below shows the existing sibling subfolders that already occupy names in PN-JV80.</p>"
			"<p>On this page, the section below is shown only as a name reference list.</p>",
			FolderName,
			FolderName);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			return HTTPInternalServerError;
		}

		size_t nUsed = (size_t) nWritten;

		if (!AppendDestinationFolderList (PNPage, nPNPageSize, &nUsed, FolderName))
		{
			return HTTPInternalServerError;
		}

		nWritten = snprintf (
			PNPage + nUsed, nPNPageSize - nUsed,
			"<p><a href=\"/dir-rename/PN-JV80/%s\">Back to rename folder</a></p>"
			"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
			"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
			"<p><a href=\"/browse\">Back to browse root</a></p>"
			"<p><a href=\"/\">Back to home</a></p>"
			"</body>"
			"</html>",
			EncodedFolder,
			EncodedFolder);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize - nUsed)
		{
			return HTTPInternalServerError;
		}

		*ppBody = PNPage;
		return HTTPOK;
	}

	static THTTPStatus HandleUploadTextProbe (
		const TNetFileServerConfig& Config,
		const char *pPath,
		const char *pParams,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		TUploadTextProbeScratch *pScratch = (TUploadTextProbeScratch *) malloc (sizeof (TUploadTextProbeScratch));
		if (pScratch == 0)
		{
			return HTTPInternalServerError;
		}

		THTTPStatus Result = HTTPInternalServerError;

		do
		{
			const char *pEncodedName = pPath + 22;   // "/upload-probe/PN-JV80/"
			if (pEncodedName[0] == '\0')
			{
				Result = HTTPNotFound;
				break;
			}

			char EncodedFolderOnly[256];
			size_t nEncodedFolderLen = strcspn (pEncodedName, "/?");

			if (nEncodedFolderLen == 0 || nEncodedFolderLen >= sizeof EncodedFolderOnly)
			{
				Result = HTTPNotFound;
				break;
			}

			memcpy (EncodedFolderOnly, pEncodedName, nEncodedFolderLen);
			EncodedFolderOnly[nEncodedFolderLen] = '\0';

			if (!URLDecode (EncodedFolderOnly, pScratch->FolderName, sizeof pScratch->FolderName))
			{
				Result = HTTPNotFound;
				break;
			}

			if (pScratch->FolderName[0] == '\0'
				|| HasSlash (pScratch->FolderName)
				|| IsDotName (pScratch->FolderName)
				|| IsRolandPNFolder (pScratch->FolderName))
			{
				Result = HTTPNotFound;
				break;
			}

			int nPathWritten = snprintf (
				pScratch->FullPath, sizeof pScratch->FullPath,
				"SD:/PN-JV80/%s", pScratch->FolderName);

			if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof pScratch->FullPath)
			{
				Result = HTTPInternalServerError;
				break;
			}

			DIR *pDir = opendir (pScratch->FullPath);
			if (pDir == 0)
			{
				Result = HTTPNotFound;
				break;
			}
			closedir (pDir);

			if (!URLEncodePathSegment (pScratch->FolderName, pScratch->EncodedFolder, sizeof pScratch->EncodedFolder))
			{
				Result = HTTPInternalServerError;
				break;
			}

			pScratch->FileName[0] = '\0';
			pScratch->FirstBytesText[0] = '\0';

			bool bHaveFileName = false;
			bool bHaveHex = false;
			bool bValidFileName = false;
			bool bValidHex = false;
			bool bHexPreviewOK = true;
			const char *pHexValue = 0;
			size_t nHexLen = 0;

			const char *pExtra = pEncodedName + nEncodedFolderLen;
			if (pExtra[0] == '/')
			{
				pExtra++;

				const char *pNextSlash = strchr (pExtra, '/');
				if (pNextSlash == 0 || pNextSlash == pExtra)
				{
					Result = HTTPNotFound;
					break;
				}

				char EncodedFileOnly[256];
				size_t nEncodedFileLen = (size_t) (pNextSlash - pExtra);
				if (nEncodedFileLen >= sizeof EncodedFileOnly)
				{
					Result = HTTPInternalServerError;
					break;
				}

				memcpy (EncodedFileOnly, pExtra, nEncodedFileLen);
				EncodedFileOnly[nEncodedFileLen] = '\0';

				if (URLDecode (EncodedFileOnly, pScratch->FileName, sizeof pScratch->FileName))
				{
					bHaveFileName = true;
				}

				pHexValue = pNextSlash + 1;
				nHexLen = strlen (pHexValue);
				bHaveHex = pHexValue[0] != '\0';
			}

			if (!bHaveFileName && !bHaveHex && pParams != 0 && pParams[0] != '\0')
			{
				const char *pParam = pParams;

				while (*pParam != '\0')
				{
					const char *pNext = strchr (pParam, '&');
					size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

					if (nParamLen >= 9 && strncmp (pParam, "filename=", 9) == 0)
					{
						char RawValue[256];
						size_t nValueLen = nParamLen - 9;

						if (nValueLen >= sizeof RawValue)
						{
							Result = HTTPInternalServerError;
							break;
						}

						for (size_t i = 0; i < nValueLen; i++)
						{
							char ch = pParam[9 + i];
							RawValue[i] = ch == '+' ? ' ' : ch;
						}

						RawValue[nValueLen] = '\0';

						if (URLDecode (RawValue, pScratch->FileName, sizeof pScratch->FileName))
						{
							bHaveFileName = true;
						}
					}
					else if (nParamLen >= 4 && strncmp (pParam, "hex=", 4) == 0)
					{
						pHexValue = pParam + 4;
						nHexLen = nParamLen - 4;
						bHaveHex = true;
					}

					if (pNext == 0)
					{
						break;
					}

					pParam = pNext + 1;
				}

				if (Result == HTTPInternalServerError)
				{
					break;
				}
			}

			if (bHaveFileName
				&& pScratch->FileName[0] != '\0'
				&& !HasSlash (pScratch->FileName)
				&& !IsDotName (pScratch->FileName))
			{
				const char *pExt = strrchr (pScratch->FileName, '.');
				if (pExt != 0
					&& (pExt[1] == 's' || pExt[1] == 'S')
					&& (pExt[2] == 'y' || pExt[2] == 'Y')
					&& (pExt[3] == 'x' || pExt[3] == 'X')
					&& pExt[4] == '\0')
				{
					bValidFileName = true;
				}
			}

			if (bHaveHex && pHexValue != 0 && nHexLen != 0 && (nHexLen % 2) == 0)
			{
				bValidHex = true;

				unsigned nPreviewBytes = (unsigned) (nHexLen / 2);
				if (nPreviewBytes > 8)
				{
					nPreviewBytes = 8;
				}

				unsigned nOut = 0;

				for (unsigned i = 0; i < nPreviewBytes; i++)
				{
					int hi = HexValue (pHexValue[i * 2]);
					int lo = HexValue (pHexValue[i * 2 + 1]);

					if (hi < 0 || lo < 0)
					{
						bValidHex = false;
						bHexPreviewOK = false;
						break;
					}

					if (i != 0)
					{
						pScratch->FirstBytesText[nOut++] = ' ';
					}

					unsigned byteValue = (unsigned) ((hi << 4) | lo);
					pScratch->FirstBytesText[nOut++] = HexDigit ((byteValue >> 4) & 0x0F);
					pScratch->FirstBytesText[nOut++] = HexDigit (byteValue & 0x0F);
				}

				pScratch->FirstBytesText[nOut] = '\0';
			}

			if (pScratch->FirstBytesText[0] == '\0')
			{
				int nTextWritten = snprintf (
					pScratch->FirstBytesText, sizeof pScratch->FirstBytesText,
					"%s", bHaveHex && !bHexPreviewOK ? "(invalid hex)" : "(none)");

				if (nTextWritten < 0 || (unsigned) nTextWritten >= sizeof pScratch->FirstBytesText)
				{
					Result = HTTPInternalServerError;
					break;
				}
			}

			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Upload probe: PN-JV80/%s</title></head>"
				"<body>"
				"<h1>Upload probe: PN-JV80/%s</h1>"
				"<p>No file has been written yet. This page reports what arrived through a normal non-multipart POST.</p>"
				"<ul>"
				"<li>Destination folder: %s</li>"
				"<li>SD path: %s</li>"
				"<li>Detected filename: %s</li>"
				"<li>Filename looks like .syx: %s</li>"
				"<li>Hex payload present: %s</li>"
				"<li>Hex payload length: %u chars</li>"
				"<li>Decoded byte length: %u bytes</li>"
				"<li>First bytes: %s</li>"
				"</ul>"
				"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->FolderName,
				pScratch->FolderName,
				pScratch->FolderName,
				pScratch->FullPath,
				bHaveFileName ? pScratch->FileName : "(none)",
				BoolText (bValidFileName),
				BoolText (bHaveHex),
				(unsigned) nHexLen,
				(unsigned) (nHexLen / 2),
				pScratch->FirstBytesText,
				pScratch->EncodedFolder,
				pScratch->EncodedFolder);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
		}
		while (0);

	free (pScratch);
	return Result;
	}

	static THTTPStatus HandleUploadTextSave (
		const TNetFileServerConfig& Config,
		const char *pPath,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		TUploadTextSaveScratch *pScratch = (TUploadTextSaveScratch *) malloc (sizeof (TUploadTextSaveScratch));
		if (pScratch == 0)
		{
			return HTTPInternalServerError;
		}

		THTTPStatus Result = HTTPInternalServerError;
		FILE *pOutput = 0;

		do
		{
			const char *pEncodedName = pPath + 21;   // "/upload-save/PN-JV80/"
			if (pEncodedName[0] == '\0')
			{
				Result = HTTPNotFound;
				break;
			}

			char EncodedFolderOnly[256];
			size_t nEncodedFolderLen = strcspn (pEncodedName, "/?");

			if (nEncodedFolderLen == 0 || nEncodedFolderLen >= sizeof EncodedFolderOnly)
			{
				Result = HTTPNotFound;
				break;
			}

			memcpy (EncodedFolderOnly, pEncodedName, nEncodedFolderLen);
			EncodedFolderOnly[nEncodedFolderLen] = '\0';

			if (!URLDecode (EncodedFolderOnly, pScratch->FolderName, sizeof pScratch->FolderName))
			{
				Result = HTTPNotFound;
				break;
			}

			if (pScratch->FolderName[0] == '\0'
				|| HasSlash (pScratch->FolderName)
				|| IsDotName (pScratch->FolderName)
				|| IsRolandPNFolder (pScratch->FolderName))
			{
				Result = HTTPNotFound;
				break;
			}

			int nPathWritten = snprintf (
				pScratch->FullPath, sizeof pScratch->FullPath,
				"SD:/PN-JV80/%s", pScratch->FolderName);

			if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof pScratch->FullPath)
			{
				Result = HTTPInternalServerError;
				break;
			}

			DIR *pDir = opendir (pScratch->FullPath);
			if (pDir == 0)
			{
				Result = HTTPNotFound;
				break;
			}
			closedir (pDir);

			if (!URLEncodePathSegment (pScratch->FolderName, pScratch->EncodedFolder, sizeof pScratch->EncodedFolder))
			{
				Result = HTTPInternalServerError;
				break;
			}

			const char *pExtra = pEncodedName + nEncodedFolderLen;
			if (pExtra[0] != '/')
			{
				Result = HTTPNotFound;
				break;
			}

			pExtra++;

			const char *pNextSlash = strchr (pExtra, '/');
			if (pNextSlash == 0 || pNextSlash == pExtra)
			{
				Result = HTTPNotFound;
				break;
			}

			char EncodedFileOnly[256];
			size_t nEncodedFileLen = (size_t) (pNextSlash - pExtra);
			if (nEncodedFileLen >= sizeof EncodedFileOnly)
			{
				Result = HTTPInternalServerError;
				break;
			}

			memcpy (EncodedFileOnly, pExtra, nEncodedFileLen);
			EncodedFileOnly[nEncodedFileLen] = '\0';

			if (!URLDecode (EncodedFileOnly, pScratch->FileName, sizeof pScratch->FileName))
			{
				Result = HTTPNotFound;
				break;
			}

			if (pScratch->FileName[0] == '\0'
				|| HasSlash (pScratch->FileName)
				|| IsDotName (pScratch->FileName))
			{
				Result = HTTPNotFound;
				break;
			}

			const char *pExt = strrchr (pScratch->FileName, '.');
			if (pExt == 0
				|| !((pExt[1] == 's' || pExt[1] == 'S')
				  && (pExt[2] == 'y' || pExt[2] == 'Y')
				  && (pExt[3] == 'x' || pExt[3] == 'X')
				  && pExt[4] == '\0'))
			{
				int nWritten = snprintf (
					PNPage, nPNPageSize,
					"<html>"
					"<head><title>Upload not executed</title></head>"
					"<body>"
					"<h1>Upload not executed</h1>"
					"<p>The selected file name is not a valid .syx file name. No file has been written.</p>"
					"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
					"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
					"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
					"<p><a href=\"/browse\">Back to browse root</a></p>"
					"<p><a href=\"/\">Back to home</a></p>"
					"</body>"
					"</html>",
					pScratch->EncodedFolder,
					pScratch->EncodedFolder);

				if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
				{
					Result = HTTPInternalServerError;
					break;
				}

				*ppBody = PNPage;
				Result = HTTPOK;
				break;
			}

			const char *pHexValue = pNextSlash + 1;
			size_t nHexLen = strlen (pHexValue);

			if (nHexLen == 0 || (nHexLen % 2) != 0)
			{
				int nWritten = snprintf (
					PNPage, nPNPageSize,
					"<html>"
					"<head><title>Upload not executed</title></head>"
					"<body>"
					"<h1>Upload not executed</h1>"
					"<p>The upload payload is missing or invalid. No file has been written.</p>"
					"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
					"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
					"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
					"<p><a href=\"/browse\">Back to browse root</a></p>"
					"<p><a href=\"/\">Back to home</a></p>"
					"</body>"
					"</html>",
					pScratch->EncodedFolder,
					pScratch->EncodedFolder);

				if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
				{
					Result = HTTPInternalServerError;
					break;
				}

				*ppBody = PNPage;
				Result = HTTPOK;
				break;
			}

			if (!URLEncodePathSegment (pScratch->FileName, pScratch->EncodedFile, sizeof pScratch->EncodedFile))
			{
				Result = HTTPInternalServerError;
				break;
			}

			int nFinalWritten = snprintf (
				pScratch->FinalPath, sizeof pScratch->FinalPath,
				"SD:/PN-JV80/%s/%s",
				pScratch->FolderName,
				pScratch->FileName);

			if (nFinalWritten < 0 || (unsigned) nFinalWritten >= sizeof pScratch->FinalPath)
			{
				Result = HTTPInternalServerError;
				break;
			}

			DIR *pTargetDir = opendir (pScratch->FinalPath);
			if (pTargetDir != 0)
			{
				closedir (pTargetDir);

				int nWritten = snprintf (
					PNPage, nPNPageSize,
					"<html>"
					"<head><title>Upload not executed</title></head>"
					"<body>"
					"<h1>Upload not executed</h1>"
					"<p>A folder with the same name already exists. No file has been written.</p>"
					"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
					"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
					"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
					"<p><a href=\"/browse\">Back to browse root</a></p>"
					"<p><a href=\"/\">Back to home</a></p>"
					"</body>"
					"</html>",
					pScratch->EncodedFolder,
					pScratch->EncodedFolder);

				if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
				{
					Result = HTTPInternalServerError;
					break;
				}

				*ppBody = PNPage;
				Result = HTTPOK;
				break;
			}

			FILE *pExisting = fopen (pScratch->FinalPath, "rb");
			if (pExisting != 0)
			{
				fclose (pExisting);

				int nWritten = snprintf (
					PNPage, nPNPageSize,
					"<html>"
					"<head><title>Upload not executed</title></head>"
					"<body>"
					"<h1>Upload not executed</h1>"
					"<p>A file with the same name already exists. Overwrite is not enabled yet, so no file has been written.</p>"
					"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
					"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
					"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
					"<p><a href=\"/browse\">Back to browse root</a></p>"
					"<p><a href=\"/\">Back to home</a></p>"
					"</body>"
					"</html>",
					pScratch->EncodedFolder,
					pScratch->EncodedFolder);

				if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
				{
					Result = HTTPInternalServerError;
					break;
				}

				*ppBody = PNPage;
				Result = HTTPOK;
				break;
			}

			int nTempWritten = snprintf (
				pScratch->TempPath, sizeof pScratch->TempPath,
				"%s.tmp",
				pScratch->FinalPath);

			if (nTempWritten < 0 || (unsigned) nTempWritten >= sizeof pScratch->TempPath)
			{
				Result = HTTPInternalServerError;
				break;
			}

			remove (pScratch->TempPath);

			pOutput = fopen (pScratch->TempPath, "wb");
			if (pOutput == 0)
			{
				Result = HTTPInternalServerError;
				break;
			}

			for (size_t i = 0; i < nHexLen; i += 2)
			{
				int hi = HexValue (pHexValue[i]);
				int lo = HexValue (pHexValue[i + 1]);

				if (hi < 0 || lo < 0)
				{
					fclose (pOutput);
					pOutput = 0;
					remove (pScratch->TempPath);

					int nWritten = snprintf (
						PNPage, nPNPageSize,
						"<html>"
						"<head><title>Upload not executed</title></head>"
						"<body>"
						"<h1>Upload not executed</h1>"
						"<p>The upload payload contains invalid hex data. No file has been written.</p>"
						"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
						"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
						"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
						"<p><a href=\"/browse\">Back to browse root</a></p>"
						"<p><a href=\"/\">Back to home</a></p>"
						"</body>"
						"</html>",
						pScratch->EncodedFolder,
						pScratch->EncodedFolder);

					if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
					{
						Result = HTTPInternalServerError;
						break;
					}

					*ppBody = PNPage;
					Result = HTTPOK;
					break;
				}

				unsigned char ByteValue = (unsigned char) ((hi << 4) | lo);
				if (fwrite (&ByteValue, 1, 1, pOutput) != 1)
				{
					Result = HTTPInternalServerError;
					break;
				}
			}

			if (Result == HTTPOK)
			{
				break;
			}

			if (Result != HTTPInternalServerError)
			{
				break;
			}

			if (fclose (pOutput) != 0)
			{
				pOutput = 0;
				remove (pScratch->TempPath);
				Result = HTTPInternalServerError;
				break;
			}
			pOutput = 0;

			if (rename (pScratch->TempPath, pScratch->FinalPath) != 0)
			{
				remove (pScratch->TempPath);
				Result = HTTPInternalServerError;
				break;
			}

			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Upload completed: PN-JV80/%s/%s</title></head>"
				"<body>"
				"<h1>Upload completed: PN-JV80/%s/%s</h1>"
				"<p>The .syx file has been uploaded to the SD card.</p>"
				"<ul>"
				"<li>Destination folder: %s</li>"
				"<li>File name: %s</li>"
				"<li>Saved size: %u bytes</li>"
				"<li>Final SD path: %s</li>"
				"</ul>"
				"<p><a href=\"/browse/PN-JV80/%s/%s\">Open file detail</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->FolderName,
				pScratch->FileName,
				pScratch->FolderName,
				pScratch->FileName,
				pScratch->FolderName,
				pScratch->FileName,
				(unsigned) (nHexLen / 2),
				pScratch->FinalPath,
				pScratch->EncodedFolder,
				pScratch->EncodedFile,
				pScratch->EncodedFolder);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
		}
		while (0);

		if (pOutput != 0)
		{
			fclose (pOutput);
			remove (pScratch->TempPath);
		}

		free (pScratch);
		return Result;
	}

struct TUploadChunkScratch
{
	char FolderName[256];
	char FullPath[768];
	char EncodedFolder[768];
	char FileName[256];
	char EncodedFile[768];
	char FinalPath[768];
	char TempPath[768];
};

static bool DecodeChunkUploadTarget (
	const char *pEncodedName,
	TUploadChunkScratch *pScratch,
	const char **ppRemainder)
{
	if (pEncodedName == 0 || pScratch == 0)
	{
		return false;
	}

	char EncodedFolderOnly[256];
	size_t nEncodedFolderLen = strcspn (pEncodedName, "/?");

	if (nEncodedFolderLen == 0 || nEncodedFolderLen >= sizeof EncodedFolderOnly)
	{
		return false;
	}

	memcpy (EncodedFolderOnly, pEncodedName, nEncodedFolderLen);
	EncodedFolderOnly[nEncodedFolderLen] = '\0';

	if (!URLDecode (EncodedFolderOnly, pScratch->FolderName, sizeof pScratch->FolderName))
	{
		return false;
	}

	if (pScratch->FolderName[0] == '\0'
		|| HasSlash (pScratch->FolderName)
		|| IsDotName (pScratch->FolderName)
		|| IsRolandPNFolder (pScratch->FolderName))
	{
		return false;
	}

	int nPathWritten = snprintf (
		pScratch->FullPath, sizeof pScratch->FullPath,
		"SD:/PN-JV80/%s", pScratch->FolderName);

	if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof pScratch->FullPath)
	{
		return false;
	}

	DIR *pDir = opendir (pScratch->FullPath);
	if (pDir == 0)
	{
		return false;
	}
	closedir (pDir);

	if (!URLEncodePathSegment (pScratch->FolderName, pScratch->EncodedFolder, sizeof pScratch->EncodedFolder))
	{
		return false;
	}

	const char *pExtra = pEncodedName + nEncodedFolderLen;
	if (pExtra[0] != '/')
	{
		return false;
	}

	pExtra++;

	const char *pNextSlash = strchr (pExtra, '/');
	size_t nEncodedFileLen = pNextSlash != 0 ? (size_t) (pNextSlash - pExtra) : strlen (pExtra);

	if (nEncodedFileLen == 0 || nEncodedFileLen >= 256)
	{
		return false;
	}

	char EncodedFileOnly[256];
	memcpy (EncodedFileOnly, pExtra, nEncodedFileLen);
	EncodedFileOnly[nEncodedFileLen] = '\0';

	if (!URLDecode (EncodedFileOnly, pScratch->FileName, sizeof pScratch->FileName))
	{
		return false;
	}

	if (pScratch->FileName[0] == '\0'
		|| HasSlash (pScratch->FileName)
		|| IsDotName (pScratch->FileName))
	{
		return false;
	}

	const char *pExt = strrchr (pScratch->FileName, '.');
	if (pExt == 0
		|| !((pExt[1] == 's' || pExt[1] == 'S')
		  && (pExt[2] == 'y' || pExt[2] == 'Y')
		  && (pExt[3] == 'x' || pExt[3] == 'X')
		  && pExt[4] == '\0'))
	{
		return false;
	}

	if (!URLEncodePathSegment (pScratch->FileName, pScratch->EncodedFile, sizeof pScratch->EncodedFile))
	{
		return false;
	}

	int nFinalWritten = snprintf (
		pScratch->FinalPath, sizeof pScratch->FinalPath,
		"SD:/PN-JV80/%s/%s",
		pScratch->FolderName,
		pScratch->FileName);

	if (nFinalWritten < 0 || (unsigned) nFinalWritten >= sizeof pScratch->FinalPath)
	{
		return false;
	}

	int nTempWritten = snprintf (
		pScratch->TempPath, sizeof pScratch->TempPath,
		"%s.tmp",
		pScratch->FinalPath);

	if (nTempWritten < 0 || (unsigned) nTempWritten >= sizeof pScratch->TempPath)
	{
		return false;
	}

	if (ppRemainder != 0)
	{
		*ppRemainder = pNextSlash != 0 ? (pNextSlash + 1) : (pExtra + nEncodedFileLen);
	}

	return true;
}

static THTTPStatus HandleUploadChunkBegin (
	const TNetFileServerConfig& Config,
	const char *pPath,
	char *PNPage,
	size_t nPNPageSize,
	const char **ppBody)
{
	if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
	{
		return HTTPNotFound;
	}

	TUploadChunkScratch *pScratch = (TUploadChunkScratch *) malloc (sizeof (TUploadChunkScratch));
	if (pScratch == 0)
	{
		return HTTPInternalServerError;
	}

	THTTPStatus Result = HTTPInternalServerError;

	do
	{
		const char *pEncodedName = pPath + 22;   // "/upload-begin/PN-JV80/"
		const char *pSizeText = 0;

		if (!DecodeChunkUploadTarget (pEncodedName, pScratch, &pSizeText))
		{
			Result = HTTPNotFound;
			break;
		}

		if (pSizeText == 0 || pSizeText[0] == '\0' || strchr (pSizeText, '/') != 0)
		{
			Result = HTTPNotFound;
			break;
		}

		char *pEnd = 0;
		unsigned long nExpectedSize = strtoul (pSizeText, &pEnd, 10);
		if (pEnd == 0 || *pEnd != '\0' || nExpectedSize == 0)
		{
			Result = HTTPNotFound;
			break;
		}

		DIR *pTargetDir = opendir (pScratch->FinalPath);
		if (pTargetDir != 0)
		{
			closedir (pTargetDir);
			Result = HTTPNotFound;
			break;
		}

		FILE *pExisting = fopen (pScratch->FinalPath, "rb");
		if (pExisting != 0)
		{
			fclose (pExisting);

			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Upload begin not executed</title></head>"
				"<body>"
				"<h1>Upload begin not executed</h1>"
				"<p>A file with the same name already exists. No temporary upload file has been created.</p>"
				"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->EncodedFolder,
				pScratch->EncodedFolder);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
			break;
		}

		remove (pScratch->TempPath);

		FILE *pTemp = fopen (pScratch->TempPath, "wb");
		if (pTemp == 0)
		{
			Result = HTTPInternalServerError;
			break;
		}

		if (fclose (pTemp) != 0)
		{
			remove (pScratch->TempPath);
			Result = HTTPInternalServerError;
			break;
		}

		int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Upload begin ready: PN-JV80/%s/%s</title></head>"
			"<body>"
			"<h1>Upload begin ready: PN-JV80/%s/%s</h1>"
			"<p>The temporary upload file has been prepared. No final file has been created yet.</p>"
			"<ul>"
			"<li>Destination folder: %s</li>"
			"<li>File name: %s</li>"
			"<li>Expected size: %lu bytes</li>"
			"<li>Temporary SD path: %s</li>"
			"</ul>"
			"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
			"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
			"<p><a href=\"/browse\">Back to browse root</a></p>"
			"<p><a href=\"/\">Back to home</a></p>"
			"</body>"
			"</html>",
			pScratch->FolderName,
			pScratch->FileName,
			pScratch->FolderName,
			pScratch->FileName,
			pScratch->FolderName,
			pScratch->FileName,
			nExpectedSize,
			pScratch->TempPath,
			pScratch->EncodedFolder);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			remove (pScratch->TempPath);
			Result = HTTPInternalServerError;
			break;
		}

		*ppBody = PNPage;
		Result = HTTPOK;
	}
	while (0);

	free (pScratch);
	return Result;
}

static THTTPStatus HandleUploadChunkData (
	const TNetFileServerConfig& Config,
	const char *pPath,
	char *PNPage,
	size_t nPNPageSize,
	const char **ppBody)
{
	if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
	{
		return HTTPNotFound;
	}

	TUploadChunkScratch *pScratch = (TUploadChunkScratch *) malloc (sizeof (TUploadChunkScratch));
	if (pScratch == 0)
	{
		return HTTPInternalServerError;
	}

	THTTPStatus Result = HTTPInternalServerError;
	FILE *pTemp = 0;

	do
	{
		const char *pEncodedName = pPath + 22;   // "/upload-chunk/PN-JV80/"
		const char *pOffsetText = 0;

		if (!DecodeChunkUploadTarget (pEncodedName, pScratch, &pOffsetText))
		{
			Result = HTTPNotFound;
			break;
		}

		if (pOffsetText == 0 || pOffsetText[0] == '\0')
		{
			Result = HTTPNotFound;
			break;
		}

		const char *pHexValue = strchr (pOffsetText, '/');
		if (pHexValue == 0 || pHexValue == pOffsetText)
		{
			Result = HTTPNotFound;
			break;
		}

		char OffsetText[32];
		size_t nOffsetLen = (size_t) (pHexValue - pOffsetText);
		if (nOffsetLen == 0 || nOffsetLen >= sizeof OffsetText)
		{
			Result = HTTPNotFound;
			break;
		}

		memcpy (OffsetText, pOffsetText, nOffsetLen);
		OffsetText[nOffsetLen] = '\0';
		pHexValue++;

		if (pHexValue[0] == '\0' || (strlen (pHexValue) % 2) != 0 || strchr (pHexValue, '/') != 0)
		{
			Result = HTTPNotFound;
			break;
		}

		char *pEnd = 0;
		unsigned long nOffset = strtoul (OffsetText, &pEnd, 10);
		if (pEnd == 0 || *pEnd != '\0')
		{
			Result = HTTPNotFound;
			break;
		}

		pTemp = fopen (pScratch->TempPath, "r+b");
		if (pTemp == 0)
		{
			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Upload chunk not executed</title></head>"
				"<body>"
				"<h1>Upload chunk not executed</h1>"
				"<p>The temporary upload file does not exist yet. Execute upload-begin first.</p>"
				"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->EncodedFolder,
				pScratch->EncodedFolder);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
			break;
		}

		if (fseek (pTemp, (long) nOffset, SEEK_SET) != 0)
		{
			Result = HTTPInternalServerError;
			break;
		}

		size_t nHexLen = strlen (pHexValue);

		for (size_t i = 0; i < nHexLen; i += 2)
		{
			int hi = HexValue (pHexValue[i]);
			int lo = HexValue (pHexValue[i + 1]);

			if (hi < 0 || lo < 0)
			{
				int nWritten = snprintf (
					PNPage, nPNPageSize,
					"<html>"
					"<head><title>Upload chunk not executed</title></head>"
					"<body>"
					"<h1>Upload chunk not executed</h1>"
					"<p>The chunk payload contains invalid hex data. The final file has not been created.</p>"
					"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
					"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
					"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
					"<p><a href=\"/browse\">Back to browse root</a></p>"
					"<p><a href=\"/\">Back to home</a></p>"
					"</body>"
					"</html>",
					pScratch->EncodedFolder,
					pScratch->EncodedFolder);

				if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
				{
					Result = HTTPInternalServerError;
					break;
				}

				*ppBody = PNPage;
				Result = HTTPOK;
				break;
			}

			unsigned char ByteValue = (unsigned char) ((hi << 4) | lo);
			if (fwrite (&ByteValue, 1, 1, pTemp) != 1)
			{
				Result = HTTPInternalServerError;
				break;
			}
		}

		if (Result == HTTPOK)
		{
			break;
		}

		if (fclose (pTemp) != 0)
		{
			pTemp = 0;
			Result = HTTPInternalServerError;
			break;
		}
		pTemp = 0;

		int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Upload chunk accepted: PN-JV80/%s/%s</title></head>"
			"<body>"
			"<h1>Upload chunk accepted: PN-JV80/%s/%s</h1>"
			"<p>The chunk has been written to the temporary upload file.</p>"
			"<ul>"
			"<li>Destination folder: %s</li>"
			"<li>File name: %s</li>"
			"<li>Offset: %lu</li>"
			"<li>Chunk size: %u bytes</li>"
			"<li>Temporary SD path: %s</li>"
			"</ul>"
			"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
			"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
			"<p><a href=\"/browse\">Back to browse root</a></p>"
			"<p><a href=\"/\">Back to home</a></p>"
			"</body>"
			"</html>",
			pScratch->FolderName,
			pScratch->FileName,
			pScratch->FolderName,
			pScratch->FileName,
			pScratch->FolderName,
			pScratch->FileName,
			nOffset,
			(unsigned) (nHexLen / 2),
			pScratch->TempPath,
			pScratch->EncodedFolder);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			Result = HTTPInternalServerError;
			break;
		}

		*ppBody = PNPage;
		Result = HTTPOK;
	}
	while (0);

	if (pTemp != 0)
	{
		fclose (pTemp);
	}

	free (pScratch);
	return Result;
}

static THTTPStatus HandleUploadChunkFinish (
	const TNetFileServerConfig& Config,
	const char *pPath,
	char *PNPage,
	size_t nPNPageSize,
	const char **ppBody)
{
	if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
	{
		return HTTPNotFound;
	}

	TUploadChunkScratch *pScratch = (TUploadChunkScratch *) malloc (sizeof (TUploadChunkScratch));
	if (pScratch == 0)
	{
		return HTTPInternalServerError;
	}

	THTTPStatus Result = HTTPInternalServerError;

	do
	{
		const char *pEncodedName = pPath + 23;   // "/upload-finish/PN-JV80/"
		const char *pSizeText = 0;

		if (!DecodeChunkUploadTarget (pEncodedName, pScratch, &pSizeText))
		{
			Result = HTTPNotFound;
			break;
		}

		if (pSizeText == 0 || pSizeText[0] == '\0' || strchr (pSizeText, '/') != 0)
		{
			Result = HTTPNotFound;
			break;
		}

		char *pEnd = 0;
		unsigned long nExpectedSize = strtoul (pSizeText, &pEnd, 10);
		if (pEnd == 0 || *pEnd != '\0' || nExpectedSize == 0)
		{
			Result = HTTPNotFound;
			break;
		}

		FILE *pTemp = fopen (pScratch->TempPath, "rb");
		if (pTemp == 0)
		{
			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Upload finish not executed</title></head>"
				"<body>"
				"<h1>Upload finish not executed</h1>"
				"<p>The temporary upload file does not exist yet. Execute upload-begin and upload-chunk first.</p>"
				"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->EncodedFolder,
				pScratch->EncodedFolder);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
			break;
		}

		long nSavedSize = -1;
		if (fseek (pTemp, 0, SEEK_END) == 0)
		{
			nSavedSize = ftell (pTemp);
		}
		fclose (pTemp);

		if (nSavedSize < 0 || (unsigned long) nSavedSize != nExpectedSize)
		{
			remove (pScratch->TempPath);

			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Upload finish not executed</title></head>"
				"<body>"
				"<h1>Upload finish not executed</h1>"
				"<p>The temporary upload size does not match the expected total size. The temporary upload file has been removed.</p>"
				"<ul>"
				"<li>Expected size: %lu bytes</li>"
				"<li>Saved size: %ld bytes</li>"
				"</ul>"
				"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				nExpectedSize,
				nSavedSize,
				pScratch->EncodedFolder,
				pScratch->EncodedFolder);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
			break;
		}

		FILE *pExisting = fopen (pScratch->FinalPath, "rb");
		if (pExisting != 0)
		{
			fclose (pExisting);
			remove (pScratch->TempPath);

			int nWritten = snprintf (
				PNPage, nPNPageSize,
				"<html>"
				"<head><title>Upload finish not executed</title></head>"
				"<body>"
				"<h1>Upload finish not executed</h1>"
				"<p>A file with the same name already exists. The temporary upload file has been removed.</p>"
				"<p><a href=\"/upload/PN-JV80/%s\">Back to upload page</a></p>"
				"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
				"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
				"<p><a href=\"/browse\">Back to browse root</a></p>"
				"<p><a href=\"/\">Back to home</a></p>"
				"</body>"
				"</html>",
				pScratch->EncodedFolder,
				pScratch->EncodedFolder);

			if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
			{
				Result = HTTPInternalServerError;
				break;
			}

			*ppBody = PNPage;
			Result = HTTPOK;
			break;
		}

		if (rename (pScratch->TempPath, pScratch->FinalPath) != 0)
		{
			Result = HTTPInternalServerError;
			break;
		}

		int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Upload completed: PN-JV80/%s/%s</title></head>"
			"<body>"
			"<h1>Upload completed: PN-JV80/%s/%s</h1>"
			"<p>The chunked upload has been finalized and the .syx file has been written to the SD card.</p>"
			"<ul>"
			"<li>Destination folder: %s</li>"
			"<li>File name: %s</li>"
			"<li>Saved size: %ld bytes</li>"
			"<li>Final SD path: %s</li>"
			"</ul>"
			"<p><a href=\"/browse/PN-JV80/%s/%s\">Open file detail</a></p>"
			"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
			"<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
			"<p><a href=\"/browse\">Back to browse root</a></p>"
			"<p><a href=\"/\">Back to home</a></p>"
			"</body>"
			"</html>",
			pScratch->FolderName,
			pScratch->FileName,
			pScratch->FolderName,
			pScratch->FileName,
			pScratch->FolderName,
			pScratch->FileName,
			nSavedSize,
			pScratch->FinalPath,
			pScratch->EncodedFolder,
			pScratch->EncodedFile,
			pScratch->EncodedFolder);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			Result = HTTPInternalServerError;
			break;
		}

		*ppBody = PNPage;
		Result = HTTPOK;
	}
	while (0);

	free (pScratch);
	return Result;
}

static THTTPStatus HandleUploadPage (
		const TNetFileServerConfig& Config,
		const char *pPath,
		const char *pParams,
		char *PNPage,
		size_t nPNPageSize,
		const char **ppBody)
	{
		if (!Config.m_bExposePNJV80 || pPath == 0 || PNPage == 0 || nPNPageSize == 0 || ppBody == 0)
		{
			return HTTPNotFound;
		}

		(void) pParams;

		const char *pEncodedName = pPath + 16;   // "/upload/PN-JV80/"
		if (pEncodedName[0] == '\0')
		{
			return HTTPNotFound;
		}

		char FolderName[256];
		if (!URLDecode (pEncodedName, FolderName, sizeof FolderName))
		{
			return HTTPNotFound;
		}

		if (FolderName[0] == '\0'
			|| HasSlash (FolderName)
			|| IsDotName (FolderName)
			|| IsRolandPNFolder (FolderName))
		{
			return HTTPNotFound;
		}

		char FullPath[768];
		int nPathWritten = snprintf (
			FullPath, sizeof FullPath,
			"SD:/PN-JV80/%s", FolderName);

		if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
		{
			return HTTPInternalServerError;
		}

		DIR *pDir = opendir (FullPath);
		if (pDir == 0)
		{
			return HTTPNotFound;
		}
		closedir (pDir);

		char EncodedFolder[768];
		if (!URLEncodePathSegment (FolderName, EncodedFolder, sizeof EncodedFolder))
		{
			return HTTPInternalServerError;
		}

int nWritten = snprintf (
			PNPage, nPNPageSize,
			"<html>"
			"<head><title>Upload .syx: PN-JV80/%s</title></head>"
			"<body>"
			"<h1>Upload .syx: PN-JV80/%s</h1>"
			"<p>Select a .syx file and upload it to the SD card using the chunked path-based upload route.</p>"
			"<ul>"
			"<li>Destination folder: %s</li>"
			"<li>SD path: %s</li>"
			"<li>Target type: .syx only</li>"
			"<li>Overwrite policy: not allowed yet</li>"
			"<li>Chunk size: 64 bytes</li>"
			"</ul>"
			"<p>Select .syx file: <input type=\"file\" id=\"pick\" accept=\".syx,.SYX\"></p>"
			"<p><input type=\"button\" value=\"Upload now\" onclick=\"PreparePathUpload();\"></p>"
			"<p id=\"uploadstatus\"></p>"
			"<script>"
			"async function FetchUploadStep(url){"
			"var resp=await fetch(url,{method:'GET',cache:'no-store'});"
			"var text=await resp.text();"
			"if(!resp.ok||text.indexOf('not executed')>=0){document.open();document.write(text);document.close();return false;}"
			"return true;"
			"}"
			"function PreparePathUpload(){"
			"var input=document.getElementById('pick');"
			"var status=document.getElementById('uploadstatus');"
			"if(!input||!input.files||input.files.length!=1){alert('Select one .syx file.');return;}"
			"var file=input.files[0];"
			"var name=file.name||'';"
			"if(!/\\.syx$/i.test(name)){alert('Select a .syx file.');return;}"
			"var reader=new FileReader();"
			"reader.onload=async function(){"
			"var bytes=new Uint8Array(reader.result);"
			"var encName=encodeURIComponent(name);"
			"var folder='%s';"
			"var chunkBytes=64;"
			"if(status)status.textContent='Preparing upload...';"
			"if(!await FetchUploadStep('/upload-begin/PN-JV80/'+folder+'/'+encName+'/'+bytes.length))return;"
			"for(var offset=0;offset<bytes.length;offset+=chunkBytes){"
			"var end=offset+chunkBytes;"
			"if(end>bytes.length)end=bytes.length;"
			"var hex='';"
			"for(var i=offset;i<end;i++){"
			"var h=bytes[i].toString(16).toUpperCase();"
			"if(h.length<2)h='0'+h;"
			"hex+=h;"
			"}"
			"if(status)status.textContent='Uploading '+end+' / '+bytes.length+' bytes...';"
			"if(!await FetchUploadStep('/upload-chunk/PN-JV80/'+folder+'/'+encName+'/'+offset+'/'+hex))return;"
			"}"
			"if(status)status.textContent='Finalizing upload...';"
			"window.location='/upload-finish/PN-JV80/'+folder+'/'+encName+'/'+bytes.length;"
			"};"
			"reader.onerror=function(){alert('Cannot read the selected file.');};"
			"reader.readAsArrayBuffer(file);"
			"}"
			"</script>"
			"<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                        "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                        "<p><a href=\"/browse\">Back to browse root</a></p>"
                        "<p><a href=\"/\">Back to home</a></p>"
                        "</body>"
                        "</html>",
                        FolderName,
                        FolderName,
                        FolderName,
                        FullPath,
                        EncodedFolder,
                        EncodedFolder);

		if (nWritten < 0 || (size_t) nWritten >= nPNPageSize)
		{
			return HTTPInternalServerError;
		}

		*ppBody = PNPage;
		return HTTPOK;
	}

	class CMiniJV880TFTPDaemon : public CTFTPDaemon
	{
	public:
		CMiniJV880TFTPDaemon (CNetSubSystem *pNetSubSystem, boolean bAllowWrite)
		:	CTFTPDaemon (pNetSubSystem),
			m_pFile (0),
			m_bAllowWrite (bAllowWrite),
			m_bWriteMode (FALSE),
			m_bKernelTransfer (FALSE),
			m_bSYXTransfer (FALSE),
			m_bCardRAMTransfer (FALSE),
			m_nBytesWritten (0)
		{
			m_StagingPath[0] = '\0';
			memset (&m_SYXPath, 0, sizeof m_SYXPath);
			memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);
		}

		virtual ~CMiniJV880TFTPDaemon (void)
		{
			if (m_pFile != 0)
			{
				fclose (m_pFile);
				m_pFile = 0;
			}
		}

		virtual boolean IsAccessAllowed (const CIPAddress *pForeignIP, const char *pFilename, boolean bWriteRequest)
		{
			(void) pForeignIP;

			DebugTX::WriteString("TFTP access ");
			DebugTX::WriteString(bWriteRequest ? "WR " : "RD ");
			DebugTX::WriteString(pFilename != 0 ? pFilename : "(null)");
			DebugTX::WriteString("\r\n");

			if (pFilename == 0)
			{
				DebugTX::WriteString("TFTP access deny\r\n");
				return FALSE;
			}

			if (bWriteRequest && !m_bAllowWrite)
			{
				DebugTX::WriteString("TFTP access deny\r\n");
				return FALSE;
			}

			TTFTPSYXUploadPath SYXPath;
			if (bWriteRequest && ParseTFTPSYXUploadPath (pFilename, &SYXPath))
			{
				DebugTX::WriteString("TFTP SYX access allow\r\n");
				return TRUE;
			}

			TTFTPCardRAMUploadPath CardRAMPath;
			if (ParseTFTPCardRAMUploadPath (pFilename, &CardRAMPath))
			{
				if (bWriteRequest)
				{
					DebugTX::WriteString("TFTP CardRAM access allow\r\n");
				}
				else
				{
					DebugTX::WriteString("TFTP CardRAM read access allow\r\n");
				}

				return TRUE;
			}

			bool bKnownName =
				   strcmp (pFilename, "kernel8-rpi4.img") == 0
				|| strcmp (pFilename, "minijv880.ini") == 0;

			if (!bKnownName)
			{
				DebugTX::WriteString("TFTP access deny\r\n");
				return FALSE;
			}

			DebugTX::WriteString("TFTP access allow\r\n");
			return TRUE;
		}

		virtual boolean FileOpen (const char *pFileName)
		{
			DebugTX::WriteString("TFTP open ");
			DebugTX::WriteString(pFileName != 0 ? pFileName : "(null)");
			DebugTX::WriteString("\r\n");

			if (pFileName == 0 || m_pFile != 0)
			{
				DebugTX::WriteString("TFTP open deny\r\n");
				return FALSE;
			}

			const char *pOpenPath = 0;

			if (strcmp (pFileName, "kernel8-rpi4.img") == 0)
			{
				TBootLayoutInfo BootLayout;
				DetectBootLayout (&BootLayout);

				pOpenPath = BootLayout.ManagedKernelActivePath;
				m_bKernelTransfer = TRUE;
			}
			else if (strcmp (pFileName, "minijv880.ini") == 0)
			{
				pOpenPath = kINIActivePath;
				m_bKernelTransfer = FALSE;
			}
			else
			{
				TTFTPCardRAMUploadPath CardRAMPath;

				if (!ParseTFTPCardRAMUploadPath (pFileName, &CardRAMPath))
				{
					DebugTX::WriteString("TFTP open wrong name\r\n");
					return FALSE;
				}

				DIR *pFinalDir = opendir (CardRAMPath.FinalPath);
				if (pFinalDir != 0)
				{
					closedir (pFinalDir);
					DebugTX::WriteString("TFTP CardRAM read is directory\r\n");
					return FALSE;
				}

				pOpenPath = CardRAMPath.FinalPath;
				m_CardRAMPath = CardRAMPath;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = FALSE;
				m_bCardRAMTransfer = TRUE;
			}

			m_bWriteMode = FALSE;
			m_nBytesWritten = 0;
			m_StagingPath[0] = '\0';

			m_pFile = fopen (pOpenPath, "rb");

			if (m_pFile == 0)
			{
				DebugTX::WriteString("TFTP open fail\r\n");
				return FALSE;
			}

			if (m_bCardRAMTransfer)
			{
				if (fseek (m_pFile, 0, SEEK_END) != 0)
				{
					fclose (m_pFile);
					m_pFile = 0;
					m_bCardRAMTransfer = FALSE;
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString("TFTP CardRAM read seek fail\r\n");
					return FALSE;
				}

				long nSize = ftell (m_pFile);

				if (fseek (m_pFile, 0, SEEK_SET) != 0)
				{
					fclose (m_pFile);
					m_pFile = 0;
					m_bCardRAMTransfer = FALSE;
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString("TFTP CardRAM read rewind fail\r\n");
					return FALSE;
				}

				if (nSize != 32768)
				{
					fclose (m_pFile);
					m_pFile = 0;
					m_bCardRAMTransfer = FALSE;
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString("TFTP CardRAM read wrong size\r\n");
					return FALSE;
				}

				DebugTX::WriteString("TFTP CardRAM read open ok\r\n");
				return TRUE;
			}

			DebugTX::WriteString("TFTP open ok\r\n");
			return TRUE;
		}

		virtual boolean FileCreate (const char *pFileName)
		{
			DebugTX::WriteString("TFTP create ");
			DebugTX::WriteString(pFileName != 0 ? pFileName : "(null)");
			DebugTX::WriteString("\r\n");

			if (!m_bAllowWrite || pFileName == 0 || m_pFile != 0)
			{
				DebugTX::WriteString("TFTP create deny\r\n");
				return FALSE;
			}

			m_bWriteMode = FALSE;
			m_bKernelTransfer = FALSE;
			m_bSYXTransfer = FALSE;
			m_bCardRAMTransfer = FALSE;
			m_nBytesWritten = 0;
			m_StagingPath[0] = '\0';
			memset (&m_SYXPath, 0, sizeof m_SYXPath);
			memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

			TTFTPSYXUploadPath SYXPath;
			if (ParseTFTPSYXUploadPath (pFileName, &SYXPath))
			{
				char FolderPath[512];

				int nFolderWritten = snprintf (
					FolderPath, sizeof FolderPath,
					"SD:/PN-JV80/%s",
					SYXPath.FolderName);

				if (nFolderWritten < 0 || (unsigned) nFolderWritten >= sizeof FolderPath)
				{
					DebugTX::WriteString("TFTP SYX folder path fail\r\n");
					return FALSE;
				}

				DIR *pFolderDir = opendir (FolderPath);
				if (pFolderDir == 0)
				{
					DebugTX::WriteString("TFTP SYX folder missing\r\n");
					return FALSE;
				}
				closedir (pFolderDir);

				DIR *pFinalDir = opendir (SYXPath.FinalPath);
				if (pFinalDir != 0)
				{
					closedir (pFinalDir);
					DebugTX::WriteString("TFTP SYX final exists\r\n");
					return FALSE;
				}

				FILE *pExisting = fopen (SYXPath.FinalPath, "rb");
				if (pExisting != 0)
				{
					fclose (pExisting);
					DebugTX::WriteString("TFTP SYX final exists\r\n");
					return FALSE;
				}

				DIR *pTempDir = opendir (SYXPath.TempPath);
				if (pTempDir != 0)
				{
					closedir (pTempDir);
					DebugTX::WriteString("TFTP SYX temp blocked\r\n");
					return FALSE;
				}

				FILE *pTempExisting = fopen (SYXPath.TempPath, "rb");
				if (pTempExisting != 0)
				{
					fclose (pTempExisting);

					if (remove (SYXPath.TempPath) != 0)
					{
						DebugTX::WriteString("TFTP SYX temp cleanup fail\r\n");
						return FALSE;
					}
				}

				m_pFile = fopen (SYXPath.TempPath, "wb");
				if (m_pFile == 0)
				{
					DebugTX::WriteString("TFTP SYX create open fail\r\n");
					return FALSE;
				}

				if (setvbuf (m_pFile, 0, _IONBF, 0) != 0)
				{
					DebugTX::WriteString("TFTP SYX create setvbuf fail\r\n");
					fclose (m_pFile);
					m_pFile = 0;
					remove (SYXPath.TempPath);
					return FALSE;
				}

				m_SYXPath = SYXPath;
				m_bWriteMode = TRUE;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = TRUE;
				m_nBytesWritten = 0;

								DebugTX::WriteString("TFTP SYX create ok\r\n");
				return TRUE;
			}

			TTFTPCardRAMUploadPath CardRAMPath;
			if (ParseTFTPCardRAMUploadPath (pFileName, &CardRAMPath))
			{
				DIR *pCardRAMDir = opendir ("SD:/CARD-RAM");
				if (pCardRAMDir == 0)
				{
					DebugTX::WriteString("TFTP CardRAM folder missing\r\n");
					return FALSE;
				}
				closedir (pCardRAMDir);

				DIR *pFinalDir = opendir (CardRAMPath.FinalPath);
				if (pFinalDir != 0)
				{
					closedir (pFinalDir);
					DebugTX::WriteString("TFTP CardRAM final exists\r\n");
					return FALSE;
				}

				FILE *pExisting = fopen (CardRAMPath.FinalPath, "rb");
				if (pExisting != 0)
				{
					fclose (pExisting);
					DebugTX::WriteString("TFTP CardRAM final exists\r\n");
					return FALSE;
				}

				DIR *pTempDir = opendir (CardRAMPath.TempPath);
				if (pTempDir != 0)
				{
					closedir (pTempDir);
					DebugTX::WriteString("TFTP CardRAM temp blocked\r\n");
					return FALSE;
				}

				FILE *pTempExisting = fopen (CardRAMPath.TempPath, "rb");
				if (pTempExisting != 0)
				{
					fclose (pTempExisting);

					if (remove (CardRAMPath.TempPath) != 0)
					{
						DebugTX::WriteString("TFTP CardRAM temp cleanup fail\r\n");
						return FALSE;
					}
				}

				m_pFile = fopen (CardRAMPath.TempPath, "wb");
				if (m_pFile == 0)
				{
					DebugTX::WriteString("TFTP CardRAM create open fail\r\n");
					return FALSE;
				}

				if (setvbuf (m_pFile, 0, _IONBF, 0) != 0)
				{
					DebugTX::WriteString("TFTP CardRAM create setvbuf fail\r\n");
					fclose (m_pFile);
					m_pFile = 0;
					remove (CardRAMPath.TempPath);
					return FALSE;
				}

				m_CardRAMPath = CardRAMPath;
				m_bWriteMode = TRUE;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = FALSE;
				m_bCardRAMTransfer = TRUE;
				m_nBytesWritten = 0;

				DebugTX::WriteString("TFTP CardRAM create ok\r\n");
				return TRUE;
			}

			const char *pTargetPath = 0;

			if (strcmp (pFileName, "kernel8-rpi4.img") == 0)
			{
				TBootLayoutInfo BootLayout;
				DetectBootLayout (&BootLayout);

				pTargetPath = BootLayout.ManagedKernelStagePath;
				m_bKernelTransfer = TRUE;
			}
			else if (strcmp (pFileName, "minijv880.ini") == 0)
			{
				pTargetPath = kINIStagePath;
				m_bKernelTransfer = FALSE;
			}
			else
			{
				DebugTX::WriteString("TFTP create wrong name\r\n");
				return FALSE;
			}

			int nWritten = snprintf (
				m_StagingPath, sizeof m_StagingPath,
				"%s",
				pTargetPath);

			if (nWritten < 0 || (unsigned) nWritten >= sizeof m_StagingPath)
			{
				DebugTX::WriteString("TFTP create path fail\r\n");
				m_StagingPath[0] = '\0';
				return FALSE;
			}

			remove (m_StagingPath);

			m_pFile = fopen (m_StagingPath, "wb");
			if (m_pFile == 0)
			{
				DebugTX::WriteString("TFTP create open fail\r\n");
				m_StagingPath[0] = '\0';
				return FALSE;
			}

			if (setvbuf (m_pFile, 0, _IONBF, 0) != 0)
			{
				DebugTX::WriteString("TFTP create setvbuf fail\r\n");
				fclose (m_pFile);
				m_pFile = 0;
				remove (m_StagingPath);
				m_StagingPath[0] = '\0';
				return FALSE;
			}

			m_bWriteMode = TRUE;
			m_nBytesWritten = 0;

			DebugTX::WriteString("TFTP create ok\r\n");
			return TRUE;
		}

		virtual boolean FileClose (void)
		{
			if (m_pFile == 0)
			{
				return FALSE;
			}

			if (!m_bWriteMode)
			{
				boolean bOK = fclose (m_pFile) == 0 ? TRUE : FALSE;
				m_pFile = 0;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = FALSE;
				m_bCardRAMTransfer = FALSE;
				m_nBytesWritten = 0;
				m_StagingPath[0] = '\0';
				memset (&m_SYXPath, 0, sizeof m_SYXPath);
				memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);
				return bOK;
			}

			bool bFileError = ferror (m_pFile) != 0;

			if (fclose (m_pFile) != 0 || bFileError)
			{
				m_pFile = 0;

				if (m_bCardRAMTransfer && m_CardRAMPath.TempPath[0] != '\0')
				{
					remove (m_CardRAMPath.TempPath);
				}
				else if (m_bSYXTransfer && m_SYXPath.TempPath[0] != '\0')
				{
					remove (m_SYXPath.TempPath);
				}
				else if (m_StagingPath[0] != '\0')
				{
					remove (m_StagingPath);
				}

				m_bWriteMode = FALSE;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = FALSE;
				m_bCardRAMTransfer = FALSE;
				m_nBytesWritten = 0;
				m_StagingPath[0] = '\0';
				memset (&m_SYXPath, 0, sizeof m_SYXPath);
				memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

				DebugTX::WriteString("TFTP close fail\r\n");
				return FALSE;
			}

			m_pFile = 0;

			if (m_bSYXTransfer)
			{
				if (m_SYXPath.FinalPath[0] == '\0' || m_SYXPath.TempPath[0] == '\0')
				{
					if (m_SYXPath.TempPath[0] != '\0')
					{
						remove (m_SYXPath.TempPath);
					}

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);

					DebugTX::WriteString("TFTP SYX close path fail\r\n");
					return FALSE;
				}

				DIR *pFinalDir = opendir (m_SYXPath.FinalPath);
				if (pFinalDir != 0)
				{
					closedir (pFinalDir);
					remove (m_SYXPath.TempPath);

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);

					DebugTX::WriteString("TFTP SYX final exists\r\n");
					return FALSE;
				}

				FILE *pExisting = fopen (m_SYXPath.FinalPath, "rb");
				if (pExisting != 0)
				{
					fclose (pExisting);
					remove (m_SYXPath.TempPath);

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);

					DebugTX::WriteString("TFTP SYX final exists\r\n");
					return FALSE;
				}

				if (rename (m_SYXPath.TempPath, m_SYXPath.FinalPath) != 0)
				{
					remove (m_SYXPath.TempPath);

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);

					DebugTX::WriteString("TFTP SYX rename fail\r\n");
					return FALSE;
				}

				DebugTX::WriteString("TFTP SYX upload ready\r\n");

				m_bWriteMode = FALSE;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = FALSE;
				m_nBytesWritten = 0;
				m_StagingPath[0] = '\0';
				memset (&m_SYXPath, 0, sizeof m_SYXPath);
				return TRUE;
			}

			if (m_bCardRAMTransfer)
			{
				unsigned long nBytesWritten = m_nBytesWritten;

				if (m_CardRAMPath.FinalPath[0] == '\0' || m_CardRAMPath.TempPath[0] == '\0')
				{
					if (m_CardRAMPath.TempPath[0] != '\0')
					{
						remove (m_CardRAMPath.TempPath);
					}

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_bCardRAMTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString("TFTP CardRAM close path fail\r\n");
					return FALSE;
				}

				if (nBytesWritten != 32768)
				{
					remove (m_CardRAMPath.TempPath);

					char LogBuf[96];
					snprintf (
						LogBuf, sizeof LogBuf,
						"TFTP CardRAM wrong size bytes=%lu\r\n",
						nBytesWritten);

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_bCardRAMTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString (LogBuf);
					return FALSE;
				}

				DIR *pFinalDir = opendir (m_CardRAMPath.FinalPath);
				if (pFinalDir != 0)
				{
					closedir (pFinalDir);
					remove (m_CardRAMPath.TempPath);

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_bCardRAMTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString("TFTP CardRAM final exists\r\n");
					return FALSE;
				}

				FILE *pExisting = fopen (m_CardRAMPath.FinalPath, "rb");
				if (pExisting != 0)
				{
					fclose (pExisting);
					remove (m_CardRAMPath.TempPath);

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_bCardRAMTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString("TFTP CardRAM final exists\r\n");
					return FALSE;
				}

				if (rename (m_CardRAMPath.TempPath, m_CardRAMPath.FinalPath) != 0)
				{
					remove (m_CardRAMPath.TempPath);

					m_bWriteMode = FALSE;
					m_bKernelTransfer = FALSE;
					m_bSYXTransfer = FALSE;
					m_bCardRAMTransfer = FALSE;
					m_nBytesWritten = 0;
					m_StagingPath[0] = '\0';
					memset (&m_SYXPath, 0, sizeof m_SYXPath);
					memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

					DebugTX::WriteString("TFTP CardRAM rename fail\r\n");
					return FALSE;
				}

				DebugTX::WriteString("TFTP CardRAM upload ready\r\n");

				m_bWriteMode = FALSE;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = FALSE;
				m_bCardRAMTransfer = FALSE;
				m_nBytesWritten = 0;
				m_StagingPath[0] = '\0';
				memset (&m_SYXPath, 0, sizeof m_SYXPath);
				memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);
				return TRUE;
			}

			if (m_bKernelTransfer && m_nBytesWritten < 65536)
			{
				unsigned long nBytesWritten = m_nBytesWritten;

				if (m_StagingPath[0] != '\0')
				{
					remove (m_StagingPath);
				}
				m_bWriteMode = FALSE;
				m_bKernelTransfer = FALSE;
				m_bSYXTransfer = FALSE;
				m_bCardRAMTransfer = FALSE;
				m_nBytesWritten = 0;
				m_StagingPath[0] = '\0';
				memset (&m_SYXPath, 0, sizeof m_SYXPath);
				memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);

				char LogBuf[96];
				snprintf (
					LogBuf, sizeof LogBuf,
					"TFTP close too small bytes=%lu\r\n",
					nBytesWritten);
				DebugTX::WriteString (LogBuf);
				return FALSE;
			}

			DebugTX::WriteString("TFTP stage ready\r\n");

			m_bWriteMode = FALSE;
			m_bKernelTransfer = FALSE;
			m_bSYXTransfer = FALSE;
			m_bCardRAMTransfer = FALSE;
			m_nBytesWritten = 0;
			m_StagingPath[0] = '\0';
			memset (&m_SYXPath, 0, sizeof m_SYXPath);
			memset (&m_CardRAMPath, 0, sizeof m_CardRAMPath);
			return TRUE;
		}

		virtual int FileRead (void *pBuffer, unsigned nCount)
		{
			if (m_pFile == 0 || pBuffer == 0 || nCount == 0 || m_bWriteMode)
			{
				DebugTX::WriteString("TFTP read fail\r\n");
				return -1;
			}

			int nRead = (int) fread (pBuffer, 1, nCount, m_pFile);

			if (nRead == 0)
			{
				DebugTX::WriteString("TFTP read eof\r\n");
			}

			return nRead;
		}

		virtual int FileWrite (const void *pBuffer, unsigned nCount)
		{
			if (m_pFile == 0 || pBuffer == 0 || nCount == 0 || !m_bWriteMode)
			{
				DebugTX::WriteString("TFTP write fail\r\n");
				return -1;
			}

			size_t nWritten = fwrite (pBuffer, 1, nCount, m_pFile);
			if (nWritten != nCount)
			{
				char LogBuf[96];
				snprintf (
					LogBuf, sizeof LogBuf,
					"TFTP write short req=%u got=%u err=%d\r\n",
					nCount,
					(unsigned) nWritten,
					ferror (m_pFile) ? 1 : 0);
				DebugTX::WriteString (LogBuf);
				return -1;
			}

			m_nBytesWritten += (unsigned long) nWritten;
			return (int) nWritten;
		}

	private:
		FILE *m_pFile;
		boolean m_bAllowWrite;
		boolean m_bWriteMode;
		boolean m_bKernelTransfer;
		boolean m_bSYXTransfer;
		boolean m_bCardRAMTransfer;
		unsigned long m_nBytesWritten;
		char m_StagingPath[256];
		TTFTPSYXUploadPath m_SYXPath;
		TTFTPCardRAMUploadPath m_CardRAMPath;
	};

	class CMiniJV880HTTPDaemon : public CHTTPDaemon

	{
	public:
		CMiniJV880HTTPDaemon (CNetSubSystem *pNetSubSystem,
		                      const TNetFileServerConfig& Config,
		                      u16 nPort,
		                      CSocket *pSocket = 0)
		:	CHTTPDaemon (pNetSubSystem, pSocket, NETFILE_HTTP_MAX_CONTENT, nPort),
			m_Config (Config),
			m_nPort (nPort)
		{
		}

		virtual CHTTPDaemon *CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
		{
			return new CMiniJV880HTTPDaemon (pNetSubSystem, m_Config, m_nPort, pSocket);
		}

		virtual THTTPStatus GetContent (const char  *pPath,
		                                const char  *pParams,
		                                const char  *pFormData,
		                                u8          *pBuffer,
		                                unsigned    *pLength,
		                                const char **ppContentType)
		{
			(void) pParams;
			(void) pFormData;

			if (pPath == 0 || pBuffer == 0 || pLength == 0 || ppContentType == 0)
			{
				return HTTPInternalServerError;
			}

			const char *pBody = 0;
                        char StatusPage[4096];
                        char BrowsePage[1600];
                        char PNPage[8192];

                        if (strcmp (pPath, "/") == 0 || strcmp (pPath, "/index.html") == 0)
                        {
	                    pBody =
		                "<html>"
		                "<head><title>MiniJV880</title></head>"
		                "<body>"
		                "<h1>MiniJV880 network server</h1>"
		                "<p>HTTP server is running.</p>"
		                "<p>Browse pages for exposed roots are available. .syx files inside PN-JV80 subfolders can be downloaded and managed from their detail page.</p>"
		                "<ul>"
		                "<li><a href=\"/browse\">Open browse root</a></li>"
		                "<li><a href=\"/cardram\">Open Data Card RAM status page</a></li>"
		                "<li style=\"list-style:none; height:0.8em;\"></li>"
		                "<li><a href=\"/kernel-status\">Open kernel status page</a></li>"
		                "<li><a href=\"/ini\">Open INI home</a></li>"
		                "<li style=\"list-style:none; height:0.8em;\"></li>"
		                "<li><a href=\"/status\">Open status page</a></li>"
		                "<li><a href=\"/maintenance\">Open maintenance overview</a></li>"
		                "</ul>"
		                "</body>"
		                "</html>";
                        }
                        else if (strcmp (pPath, "/status") == 0)
                        {
	                    int nWritten = snprintf (
		                StatusPage, sizeof StatusPage,
		                "<html>"
		                "<head><title>MiniJV880 network status</title></head>"
		                "<body>"
		                "<h1>MiniJV880 network status</h1>"
		                "<ul>"
		                "<li>Hostname: %s</li>"
		                "<li>Port: %u</li>"
		                "<li>DHCP: %s</li>"
		                "<li>Configured IP: %s</li>"
		                "<li>Configured mask: %s</li>"
		                "<li>Configured gateway: %s</li>"
		                "<li>Expose PN-JV80: %s</li>"
		                "<li>Expose roms: %s</li>"
		                "<li>Write enabled: %s</li>"
		                "</ul>"
		                "<p>Exposed roots can be browsed over HTTP. .syx files inside PN-JV80 subfolders can be downloaded and managed from their detail page.</p>"
		                "<p style=\"margin:0;\"><a href=\"/browse\">Open browse root</a></p>"
		                "<p style=\"margin:0;\"><a href=\"/cardram\">Open Data Card RAM status page</a></p>"
		                "<p style=\"margin:0.8em 0 0 0;\"><a href=\"/kernel-status\">Open kernel status page</a></p>"
		                "<p style=\"margin:0;\"><a href=\"/ini\">Open INI home</a></p>"
		                "<p style=\"margin:0.8em 0 0 0;\"><a href=\"/status.txt\">Open plain text status endpoint</a></p>"
		                "<p style=\"margin:0;\"><a href=\"/maintenance\">Open maintenance overview</a></p>"
		                "<p style=\"margin:0.8em 0 0 0;\"><a href=\"/\">Back to home</a></p>"
		                "</body>"
		                "</html>",
		                m_Config.m_HostName.empty () ? "minijv880" : m_Config.m_HostName.c_str (),
		                m_Config.m_nPort,
		                BoolText (m_Config.m_bDHCP),
		                m_Config.m_IP.empty () ? "(dhcp)" : m_Config.m_IP.c_str (),
		                m_Config.m_Mask.empty () ? "(dhcp)" : m_Config.m_Mask.c_str (),
		                m_Config.m_Gateway.empty () ? "(dhcp)" : m_Config.m_Gateway.c_str (),
		                BoolText (m_Config.m_bExposePNJV80),
		                BoolText (m_Config.m_bExposeRoms),
		                BoolText (m_Config.m_bWriteEnable));

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                      }

                        else if (strcmp (pPath, "/cardram") == 0)
                        {
	                   const char *pActivePath = MiniJV880_GetCardRamActivePath ();
	                   const char *pTmpPath = MiniJV880_GetCardRamTmpPath ();
	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   const char *pCurrentPath = MiniJV880_GetCardRamCurrentPath ();
	                   const char *pLegacyPath = MiniJV880_GetCardRamLegacyPath ();
	                   bool bUsingCollection = MiniJV880_GetCardRamUsingCollection () != 0;

	                   char ActiveSDPath[256];
	                   char TmpSDPath[256];
	                   char CollectionSDPath[256];
	                   char CurrentSDPath[256];
	                   char LegacySDPath[256];

	                   if (pActivePath == 0) pActivePath = "";
	                   if (pTmpPath == 0) pTmpPath = "";
	                   if (pCollectionDir == 0) pCollectionDir = "";
	                   if (pCurrentPath == 0) pCurrentPath = "";
	                   if (pLegacyPath == 0) pLegacyPath = "";

	                   int nPathWritten = snprintf (
		               ActiveSDPath, sizeof ActiveSDPath,
		               strncmp (pActivePath, "SD:", 3) == 0 ? "%s" : "SD:/%s",
		               pActivePath);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof ActiveSDPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   nPathWritten = snprintf (
		               TmpSDPath, sizeof TmpSDPath,
		               strncmp (pTmpPath, "SD:", 3) == 0 ? "%s" : "SD:/%s",
		               pTmpPath);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof TmpSDPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   nPathWritten = snprintf (
		               CollectionSDPath, sizeof CollectionSDPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s" : "SD:/%s",
		               pCollectionDir);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof CollectionSDPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   nPathWritten = snprintf (
		               CurrentSDPath, sizeof CurrentSDPath,
		               strncmp (pCurrentPath, "SD:", 3) == 0 ? "%s" : "SD:/%s",
		               pCurrentPath);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof CurrentSDPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   nPathWritten = snprintf (
		               LegacySDPath, sizeof LegacySDPath,
		               strncmp (pLegacyPath, "SD:", 3) == 0 ? "%s" : "SD:/%s",
		               pLegacyPath);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof LegacySDPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bActiveExists = false;
	                   bool bTmpExists = false;
	                   bool bCurrentExists = false;
	                   bool bLegacyExists = false;
	                   bool bCollectionExists = false;

	                   char ActiveSizeText[64];
	                   char TmpSizeText[64];
	                   char CurrentSizeText[64];
	                   char LegacySizeText[64];
	                   if (!GetKernelFileStatusText (
	                           ActiveSDPath,
	                           &bActiveExists,
	                           ActiveSizeText,
	                           sizeof ActiveSizeText)
	                       || !GetKernelFileStatusText (
	                           CurrentSDPath,
	                           &bCurrentExists,
	                           CurrentSizeText,
	                           sizeof CurrentSizeText)
	                       || !GetKernelFileStatusText (
	                           LegacySDPath,
	                           &bLegacyExists,
	                           LegacySizeText,
	                           sizeof LegacySizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   DIR *pCollection = opendir (CollectionSDPath);
	                   if (pCollection != 0)
	                   {
		              bCollectionExists = true;
		              closedir (pCollection);
	                   }

	                   const char *pActiveCardName = strrchr (pActivePath, '/');
	                   pActiveCardName = pActiveCardName != 0 ? pActiveCardName + 1 : pActivePath;

	                   if (pActiveCardName == 0 || pActiveCardName[0] == '\0')
	                   {
		              pActiveCardName = "(none)";
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 Data Card RAM</title></head>"
		               "<body>"
		               "<h1>MiniJV880 Data Card RAM</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<ul>"
		               "<li>Active mode: %s</li>"
		               "<li>Active card name: <b>%s</b></li>"
		               "<li>Collection directory present: %s</li>"
		               "</ul>"
		               "<h2>Select Data Card for next boot</h2>"
		               "<p>Open the collection list and select one of the existing .bin files in SD:/CARD-RAM/.</p>"
		               "<p><a href=\"/cardram-list\">Open Data Card collection list</a></p>"
		               "<p><a href=\"/cardram.txt\">Open plain text Data Card RAM endpoint</a></p>"
		               "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
		               "<p><a href=\"/status\">Back to status</a></p>"
		               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               bUsingCollection ? "collection" : "legacy",
		               pActiveCardName,
		               BoolText (bCollectionExists));

	                   if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }
                      
                        else if (strcmp (pPath, "/cardram-list-test") == 0
                              || strcmp (pPath, "/cardram-list") == 0)
                        {
	                   const unsigned kCardsPerPage = 20;
	                   unsigned nPage = 0;

	                   if (strcmp (pPath, "/cardram-list") == 0
	                       && pParams != 0
	                       && pParams[0] != '\0')
	                   {
		              const char *pParam = pParams;

		              while (*pParam != '\0')
		              {
		                  const char *pNext = strchr (pParam, '&');
		                  size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

		                  if (nParamLen >= 5 && strncmp (pParam, "page=", 5) == 0)
		                  {
			             unsigned nParsedPage = 0;
			             bool bPageOK = true;

			             for (size_t i = 5; i < nParamLen; i++)
			             {
					char ch = pParam[i];

					if (ch < '0' || ch > '9')
					{
					    bPageOK = false;
					    break;
					}

					nParsedPage = nParsedPage * 10 + (unsigned)(ch - '0');

					if (nParsedPage > 999)
					{
					    bPageOK = false;
					    break;
					}
			             }

			             if (!bPageOK)
			             {
					return HTTPNotFound;
			             }

			             nPage = nParsedPage;
			             break;
		                  }

		                  if (pNext == 0)
		                  {
			             break;
		                  }

		                  pParam = pNext + 1;
		              }
	                   }

	                   unsigned nTotalCards = 0;

	                   DIR *pCountDir = opendir ("SD:/CARD-RAM");
	                   if (pCountDir != 0)
	                   {
		              for (;;)
		              {
		                  struct dirent *pEntry = readdir (pCountDir);
		                  if (pEntry == 0)
		                  {
			             break;
		                  }

		                  if (IsCardRamBINFileName (pEntry->d_name))
		                  {
			             nTotalCards++;
		                  }
		              }

		              closedir (pCountDir);
	                   }

	                   unsigned nTotalPages =
		              nTotalCards == 0
		                  ? 1
		                  : (nTotalCards + kCardsPerPage - 1) / kCardsPerPage;

	                   if (nPage >= nTotalPages)
	                   {
		              nPage = nTotalPages - 1;
	                   }

	                   unsigned nSkip = nPage * kCardsPerPage;

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 Data Card collection list</title></head>"
		               "<body>"
		               "<h1>MiniJV880 Data Card collection list</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<p>Only .bin files directly inside SD:/CARD-RAM/ are listed.</p>"
		               "<p>Data Cards available: %u</p>"
		               "<p>Page: %u/%u</p>"
		               "<ul>",
		               nTotalCards,
		               nPage + 1,
		               nTotalPages);

	                   if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

		                   size_t nUsed = (size_t)nWritten;
		                   unsigned nListed = 0;
		                   bool bHasMore = nPage + 1 < nTotalPages;

		                   DIR *pDirTest = opendir ("SD:/CARD-RAM");
		                   if (pDirTest == 0)
		                   {
			              nWritten = snprintf (
			                  StatusPage + nUsed,
			                  sizeof StatusPage - nUsed,
			                  "</ul>"
			                  "<p style=\"color:#C00000;\">CARD-RAM directory is not available.</p>");

			              if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage - nUsed)
			              {
				          return HTTPInternalServerError;
			              }

			              nUsed += (size_t)nWritten;
		                   }
		                   else
		                   {
			              closedir (pDirTest);

			              char LastName[256];
			              LastName[0] = '\0';
			              bool bHaveLastName = false;

			              for (unsigned nIndex = 0; nIndex < nSkip + kCardsPerPage; nIndex++)
			              {
			                  char NextName[256];
			                  bool bFound = false;

			                  if (!FindNextCardRamBINName (
				                  bHaveLastName ? LastName : 0,
				                  NextName,
				                  sizeof NextName,
				                  &bFound))
			                  {
				             return HTTPInternalServerError;
			                  }

			                  if (!bFound)
			                  {
				             break;
			                  }

			                  int nNameWritten = snprintf (
				             LastName, sizeof LastName,
				             "%s",
				             NextName);

			                  if (nNameWritten < 0 || (unsigned) nNameWritten >= sizeof LastName)
			                  {
				             return HTTPInternalServerError;
			                  }

			                  bHaveLastName = true;

			                  if (nIndex < nSkip)
			                  {
				             continue;
			                  }

			                  char EncodedName[512];
			                  if (!URLEncodePathSegment (NextName, EncodedName, sizeof EncodedName))
			                  {
				             return HTTPInternalServerError;
			                  }

			                  nWritten = snprintf (
				             StatusPage + nUsed,
				             sizeof StatusPage - nUsed,
				             "<li><a href=\"/cardram-select?name=%s\">%s</a></li>",
				             EncodedName,
				             NextName);

			                  if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage - nUsed)
			                  {
				             bHasMore = true;
				             break;
			                  }

			                  nUsed += (size_t)nWritten;
			                  nListed++;
			              }

			              if (nListed == 0)
			              {
			                  nWritten = snprintf (
				             StatusPage + nUsed,
				             sizeof StatusPage - nUsed,
				             "<li>No Data Card .bin files found on this page.</li>");

			                  if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage - nUsed)
			                  {
				             return HTTPInternalServerError;
			                  }

			                  nUsed += (size_t)nWritten;
			              }

			              nWritten = snprintf (
			                  StatusPage + nUsed,
			                  sizeof StatusPage - nUsed,
			                  "</ul>"
			                  "<p>Listed cards on this page: %u</p>",
			                  nListed);

			              if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage - nUsed)
			              {
			                  return HTTPInternalServerError;
			              }

			              nUsed += (size_t)nWritten;

			              if (nPage > 0)
			              {
			                  nWritten = snprintf (
				             StatusPage + nUsed,
				             sizeof StatusPage - nUsed,
				             "<p><a href=\"/cardram-list?page=%u\">Previous page</a></p>",
				             nPage - 1);

			                  if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage - nUsed)
			                  {
				             return HTTPInternalServerError;
			                  }

			                  nUsed += (size_t)nWritten;
			              }

			              if (bHasMore)
			              {
			                  nWritten = snprintf (
				             StatusPage + nUsed,
				             sizeof StatusPage - nUsed,
				             "<p><a href=\"/cardram-list?page=%u\">Next page</a></p>",
				             nPage + 1);

			                  if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage - nUsed)
			                  {
				             return HTTPInternalServerError;
			                  }

			                  nUsed += (size_t)nWritten;
			              }
		                   }

	                   nWritten = snprintf (
		               StatusPage + nUsed,
		               sizeof StatusPage - nUsed,
		               "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
		               "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
		               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>");

	                   if (nWritten < 0 || (unsigned)nWritten >= sizeof StatusPage - nUsed)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }

                        else if (strcmp (pPath, "/cardram-select-test") == 0
                              || strcmp (pPath, "/cardram-select") == 0)
                        {
	                   char CardName[256];
	                   snprintf (CardName, sizeof CardName, "TestCard.bin");

	                   if (strcmp (pPath, "/cardram-select") == 0)
	                   {
		              bool bHaveNameParam = false;
		              bool bRequestedNameInvalid = false;
		              CardName[0] = '\0';

		              if (pParams != 0 && pParams[0] != '\0')
		              {
		                  const char *pParam = pParams;

		                  while (*pParam != '\0')
		                  {
			             const char *pNext = strchr (pParam, '&');
			             size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

			             if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
			             {
					bHaveNameParam = true;

					char RawValue[256];
					size_t nValueLen = nParamLen - 5;
					if (nValueLen >= sizeof RawValue)
					{
					    return HTTPInternalServerError;
					}

					for (size_t i = 0; i < nValueLen; i++)
					{
					    char ch = pParam[5 + i];
					    RawValue[i] = ch == '+' ? ' ' : ch;
					}

					RawValue[nValueLen] = '\0';

					if (!URLDecode (RawValue, CardName, sizeof CardName))
					{
					    bRequestedNameInvalid = true;
					    CardName[0] = '\0';
					}

					break;
			             }

			             if (pNext == 0)
			             {
					break;
			             }

			             pParam = pNext + 1;
		                  }
		              }

		              if (!bHaveNameParam || bRequestedNameInvalid || !IsCardRamBINFileName (CardName))
		              {
		                  return HTTPNotFound;
		              }
	                   }

	                   const char *pCardName = CardName;
	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   const char *pActivePath = MiniJV880_GetCardRamActivePath ();
	                   bool bUsingCollection = MiniJV880_GetCardRamUsingCollection () != 0;

	                   if (pCollectionDir == 0) pCollectionDir = "";
	                   if (pActivePath == 0) pActivePath = "";

	                   if (!IsCardRamBINFileName (pCardName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   char TargetPath[256];
	                   int nPathWritten = snprintf (
		               TargetPath, sizeof TargetPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s/%s" : "SD:/%s/%s",
		               pCollectionDir,
		               pCardName);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof TargetPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetExists = false;
	                   char TargetSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           TargetPath,
	                           &bTargetExists,
	                           TargetSizeText,
	                           sizeof TargetSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetSizeOK = false;
	                   struct stat TargetStat;
	                   if (stat (TargetPath, &TargetStat) == 0
	                       && !S_ISDIR (TargetStat.st_mode)
	                       && TargetStat.st_size == 32768)
	                   {
		              bTargetSizeOK = true;
	                   }

	                   bool bAlreadyActive = false;
	                   if (bUsingCollection)
	                   {
		              const char *pLastSlash = strrchr (pActivePath, '/');
		              const char *pActiveName = pLastSlash != 0 ? pLastSlash + 1 : pActivePath;
		              bAlreadyActive = strcmp (pActiveName, pCardName) == 0;
	                   }

	                   char EncodedName[512];
	                   if (!URLEncodePathSegment (pCardName, EncodedName, sizeof EncodedName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 Data Card select precheck</title></head>"
		               "<body>"
		               "<h1>MiniJV880 Data Card select precheck</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<p>This precheck validates the requested Data Card before selection.</p>"
		               "<ul>"
		               "<li>Requested card: %s</li>"
		               "<li>Target path: %s</li>"
		               "<li>Target file present: %s (%s)</li>"
		               "<li>Target size valid: %s</li>"
		               "<li>Current active path: %s</li>"
		               "<li>Already active: %s</li>"
		               "</ul>"
		               "<p>If selected, this card will become active only after reboot.</p>"
		               "<p><b>Before changing the selection, MiniJV880 will flush the currently active Data Card.</b></p>"
		               "<p><a href=\"/cardram-select-exec?name=%s\" %s>Select this card for next boot</a></p>"
		               "<p><a href=\"/cardram-rename?name=%s\">Rename this .bin file</a></p>"
		               "<p><a href=\"/cardram-delete?name=%s\">Delete this .bin file</a></p>"
		               "<p><a href=\"/cardram-select?name=%s\">Refresh this precheck</a></p>"
		               "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
		               "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
		               "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
		               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               pCardName,
		               TargetPath,
		               BoolText (bTargetExists),
		               KernelStatusHTML (TargetSizeText),
		               BoolText (bTargetSizeOK),
		               pActivePath,
		               BoolText (bAlreadyActive),
		               EncodedName,
		               ActionLinkStyle (),
		               EncodedName,
		               EncodedName,
		               EncodedName);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }

                        else if (strcmp (pPath, "/cardram-rename") == 0)
                        {
	                   char CardName[256];
	                   bool bHaveNameParam = false;
	                   bool bRequestedNameInvalid = false;
	                   CardName[0] = '\0';

	                   if (pParams != 0 && pParams[0] != '\0')
	                   {
		              const char *pParam = pParams;

		              while (*pParam != '\0')
		              {
		                  const char *pNext = strchr (pParam, '&');
		                  size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

		                  if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
		                  {
			             bHaveNameParam = true;

			             char RawValue[256];
			             size_t nValueLen = nParamLen - 5;
			             if (nValueLen >= sizeof RawValue)
			             {
			                return HTTPInternalServerError;
			             }

			             for (size_t i = 0; i < nValueLen; i++)
			             {
			                char ch = pParam[5 + i];
			                RawValue[i] = ch == '+' ? ' ' : ch;
			             }

			             RawValue[nValueLen] = '\0';

			             if (!URLDecode (RawValue, CardName, sizeof CardName))
			             {
			                bRequestedNameInvalid = true;
			                CardName[0] = '\0';
			             }

			             break;
		                  }

		                  if (pNext == 0)
		                  {
			             break;
		                  }

		                  pParam = pNext + 1;
		              }
	                   }

	                   if (!bHaveNameParam || bRequestedNameInvalid || !IsCardRamBINFileName (CardName))
	                   {
		              return HTTPNotFound;
	                   }

	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   if (pCollectionDir == 0) pCollectionDir = "";

	                   char TargetPath[256];
	                   int nPathWritten = snprintf (
		               TargetPath, sizeof TargetPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s/%s" : "SD:/%s/%s",
		               pCollectionDir,
		               CardName);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof TargetPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetExists = false;
	                   char TargetSizeText[64];
	                   if (!GetKernelFileStatusText (
		                   TargetPath,
		                   &bTargetExists,
		                   TargetSizeText,
		                   sizeof TargetSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetRegularFile = false;
	                   struct stat TargetStat;
	                   if (stat (TargetPath, &TargetStat) == 0
	                       && !S_ISDIR (TargetStat.st_mode))
	                   {
		              bTargetRegularFile = true;
	                   }

	                   char EncodedName[512];
	                   if (!URLEncodePathSegment (CardName, EncodedName, sizeof EncodedName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 Data Card rename precheck</title></head>"
		               "<body>"
		               "<h1>MiniJV880 Data Card rename precheck</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<p>This precheck validates the requested Data Card before rename.</p>"
		               "<ul>"
		               "<li>Requested card: %s</li>"
		               "<li>Current path: %s</li>"
		               "<li>Current file present: %s (%s)</li>"
		               "<li>Current path is a regular file: %s</li>"
		               "</ul>"
		               "<p>Rename rules for the next step:</p>"
		               "<ul>"
		               "<li>Use a single .bin file name</li>"
		               "<li>No slash or backslash</li>"
		               "<li>No overwrite</li>"
		               "</ul>"
		               "<form action=\"/cardram-rename-exec\" method=\"get\">"
		               "<input type=\"hidden\" name=\"name\" value=\"%s\">"
		               "<p>New Data Card file name:</p>"
		               "<p><input type=\"text\" name=\"newname\" size=\"40\"></p>"
		               "<p><input type=\"submit\" value=\"Execute rename now\"></p>"
		               "</form>"
		               "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
		               "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
		               "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
		               "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
		               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               CardName,
		               TargetPath,
		               BoolText (bTargetExists),
		               KernelStatusHTML (TargetSizeText),
		               BoolText (bTargetRegularFile),
		               EncodedName,
		               EncodedName);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }

                        else if (strcmp (pPath, "/cardram-rename-exec") == 0)
                        {
	                   char CardName[256];
	                   char NewName[256];
	                   bool bHaveNameParam = false;
	                   bool bHaveNewNameParam = false;
	                   bool bRequestedNameInvalid = false;
	                   bool bRequestedNewNameInvalid = false;
	                   CardName[0] = '\0';
	                   NewName[0] = '\0';

	                   if (pParams != 0 && pParams[0] != '\0')
	                   {
		              const char *pParam = pParams;

		              while (*pParam != '\0')
		              {
		                  const char *pNext = strchr (pParam, '&');
		                  size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

		                  if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
		                  {
			             bHaveNameParam = true;

			             char RawValue[256];
			             size_t nValueLen = nParamLen - 5;
			             if (nValueLen >= sizeof RawValue)
			             {
			                return HTTPInternalServerError;
			             }

			             for (size_t i = 0; i < nValueLen; i++)
			             {
			                char ch = pParam[5 + i];
			                RawValue[i] = ch == '+' ? ' ' : ch;
			             }

			             RawValue[nValueLen] = '\0';

			             if (!URLDecode (RawValue, CardName, sizeof CardName))
			             {
			                bRequestedNameInvalid = true;
			                CardName[0] = '\0';
			             }
		                  }
		                  else if (nParamLen >= 8 && strncmp (pParam, "newname=", 8) == 0)
		                  {
			             bHaveNewNameParam = true;

			             char RawValue[256];
			             size_t nValueLen = nParamLen - 8;
			             if (nValueLen >= sizeof RawValue)
			             {
			                return HTTPInternalServerError;
			             }

			             for (size_t i = 0; i < nValueLen; i++)
			             {
			                char ch = pParam[8 + i];
			                RawValue[i] = ch == '+' ? ' ' : ch;
			             }

			             RawValue[nValueLen] = '\0';

			             if (!URLDecode (RawValue, NewName, sizeof NewName))
			             {
			                bRequestedNewNameInvalid = true;
			                NewName[0] = '\0';
			             }
		                  }

		                  if (pNext == 0)
		                  {
			             break;
		                  }

		                  pParam = pNext + 1;
		              }
	                   }

	                   if (!bHaveNameParam
		               || !bHaveNewNameParam
		               || bRequestedNameInvalid
		               || bRequestedNewNameInvalid
		               || !IsCardRamBINFileName (CardName)
		               || !IsCardRamBINFileName (NewName))
	                   {
		              return HTTPNotFound;
	                   }

	                   char EncodedName[512];
	                   char EncodedNewName[512];
	                   if (!URLEncodePathSegment (CardName, EncodedName, sizeof EncodedName)
		               || !URLEncodePathSegment (NewName, EncodedNewName, sizeof EncodedNewName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   const char *pCurrentPath = MiniJV880_GetCardRamCurrentPath ();
	                   const char *pActivePath = MiniJV880_GetCardRamActivePath ();
	                   bool bUsingCollection = MiniJV880_GetCardRamUsingCollection () != 0;

	                   if (pCollectionDir == 0) pCollectionDir = "";
	                   if (pCurrentPath == 0 || pCurrentPath[0] == '\0') pCurrentPath = "SD:/CARD-RAM/current.txt";
	                   if (pActivePath == 0) pActivePath = "";

	                   char OldPath[256];
	                   char NewPath[256];

	                   int nOldPathWritten = snprintf (
		               OldPath, sizeof OldPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s/%s" : "SD:/%s/%s",
		               pCollectionDir,
		               CardName);

	                   int nNewPathWritten = snprintf (
		               NewPath, sizeof NewPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s/%s" : "SD:/%s/%s",
		               pCollectionDir,
		               NewName);

	                   if (nOldPathWritten < 0 || (unsigned) nOldPathWritten >= sizeof OldPath
		               || nNewPathWritten < 0 || (unsigned) nNewPathWritten >= sizeof NewPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bOldExists = false;
	                   char OldSizeText[64];
	                   if (!GetKernelFileStatusText (
		                   OldPath,
		                   &bOldExists,
		                   OldSizeText,
		                   sizeof OldSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bOldRegularFile = false;
	                   struct stat OldStat;
	                   if (stat (OldPath, &OldStat) == 0 && !S_ISDIR (OldStat.st_mode))
	                   {
		              bOldRegularFile = true;
	                   }

	                   if (!bOldExists || !bOldRegularFile)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 Data Card rename not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 Data Card rename not executed</h1>"
			          "<p>The source Data Card file is missing or is not a regular file.</p>"
			          "<ul>"
			          "<li>Source card: %s</li>"
			          "<li>Source path: %s</li>"
			          "<li>Source present: %s (%s)</li>"
			          "</ul>"
			          "<p>No file was modified.</p>"
			          "<p><a href=\"/cardram-rename?name=%s\">Back to rename precheck</a></p>"
			          "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
			          "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          CardName,
			          OldPath,
			          BoolText (bOldExists),
			          KernelStatusHTML (OldSizeText),
			          EncodedName,
			          EncodedName);

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else if (strcmp (CardName, NewName) == 0)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 Data Card rename not executed</title></head>"
			          "<body>"
			          "<h1>MiniJV880 Data Card rename not executed</h1>"
			          "<p>The requested new file name is identical to the current file name.</p>"
			          "<ul>"
			          "<li>Current card: %s</li>"
			          "<li>Current path: %s</li>"
			          "</ul>"
			          "<p>No file was modified.</p>"
			          "<p><a href=\"/cardram-rename?name=%s\">Back to rename precheck</a></p>"
			          "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
			          "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          CardName,
			          OldPath,
			          EncodedName,
			          EncodedName);

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              bool bNewExists = false;
		              char NewSizeText[64];
		              if (!GetKernelFileStatusText (
				              NewPath,
				              &bNewExists,
				              NewSizeText,
				              sizeof NewSizeText))
		              {
			             return HTTPInternalServerError;
		              }

		              bool bNewRegularFile = false;
		              struct stat NewStat;
		              if (stat (NewPath, &NewStat) == 0 && !S_ISDIR (NewStat.st_mode))
		              {
			             bNewRegularFile = true;
		              }

		              if (bNewExists || bNewRegularFile)
		              {
			             int nWritten = snprintf (
				         StatusPage, sizeof StatusPage,
				         "<html>"
				         "<head><title>MiniJV880 Data Card rename not executed</title></head>"
				         "<body>"
				         "<h1 style=\"color:#C00000;\">MiniJV880 Data Card rename not executed</h1>"
				         "<p>The requested destination file name already exists.</p>"
				         "<ul>"
				         "<li>Source card: %s</li>"
				         "<li>Source path: %s</li>"
				         "<li>Requested new card name: %s</li>"
				         "<li>Destination path: %s</li>"
				         "<li>Destination present: %s (%s)</li>"
				         "</ul>"
				         "<p>No file was modified.</p>"
				         "<p><a href=\"/cardram-rename?name=%s\">Back to rename precheck</a></p>"
				         "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
				         "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
				         "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
				         "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
				         "<p><a href=\"/\">Back to home</a></p>"
				         "</body>"
				         "</html>",
				         CardName,
				         OldPath,
				         NewName,
				         NewPath,
				         BoolText (bNewExists),
				         KernelStatusHTML (NewSizeText),
				         EncodedName,
				         EncodedName);

			             if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			             {
				            return HTTPInternalServerError;
			             }

			             pBody = StatusPage;
		              }
		              else
		              {
			             bool bSelectedForNextBoot = false;
			             FILE *pCurrentInput = fopen (pCurrentPath, "rb");
			             if (pCurrentInput != 0)
			             {
				            char SelectedName[256];
				            size_t nRead = fread (SelectedName, 1, sizeof SelectedName - 1, pCurrentInput);
				            fclose (pCurrentInput);
				            SelectedName[nRead] = '\0';

				            while (nRead > 0
					            && (SelectedName[nRead - 1] == '\n' || SelectedName[nRead - 1] == '\r'))
				            {
					       SelectedName[nRead - 1] = '\0';
					       nRead--;
				            }

				            bSelectedForNextBoot = strcmp (SelectedName, CardName) == 0;
			             }

			             bool bAlreadyActive = false;
			             if (bUsingCollection)
			             {
				            const char *pLastSlash = strrchr (pActivePath, '/');
				            const char *pActiveName = pLastSlash != 0 ? pLastSlash + 1 : pActivePath;
				            bAlreadyActive = strcmp (pActiveName, CardName) == 0;
			             }

			             if (bAlreadyActive)
			             {
				            MiniJV880_FlushCardRamIfNeededNow ();
			             }

			             bool bRenameOK = rename (OldPath, NewPath) == 0;
			             bool bSelectionWriteOK = true;
			             bool bRollbackOK = true;
			             bool bRolledBackAfterSelectionWriteFail = false;

			             if (bRenameOK && bSelectedForNextBoot)
			             {
				            bSelectionWriteOK = WriteCardRamCurrentSelectionFile (NewName);
				            if (!bSelectionWriteOK)
				            {
					       bRollbackOK = rename (NewPath, OldPath) == 0;
					       if (bRollbackOK)
					       {
						      bRenameOK = false;
						      bRolledBackAfterSelectionWriteFail = true;
					       }
				            }
			             }

			             int nWritten = snprintf (
				         StatusPage, sizeof StatusPage,
				         "<html>"
				         "<head><title>%s</title></head>"
				         "<body>"
				         "<h1%s>%s</h1>"
				         "<ul>"
				         "<li>Old card name: %s</li>"
				         "<li>Old path: %s</li>"
				         "<li>New card name: %s</li>"
				         "<li>New path: %s</li>"
				         "<li>Active card matched old name: %s</li>"
				         "<li>Selected for next boot before rename: %s</li>"
				         "<li>Selection file: %s</li>"
				         "</ul>"
				         "%s"
				         "<p><a href=\"/cardram-select?name=%s\">Open renamed card select precheck</a></p>"
				         "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
				         "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
				         "%s"
				         "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
				         "<p><a href=\"/\">Back to home</a></p>"
				         "</body>"
				         "</html>",
				         (bRenameOK && bSelectionWriteOK)
				             ? "MiniJV880 Data Card rename completed"
				             : "MiniJV880 Data Card rename not completed",
				         (bRenameOK && bSelectionWriteOK) ? "" : " style=\"color:#C00000;\"",
				         (bRenameOK && bSelectionWriteOK)
				             ? "MiniJV880 Data Card rename completed"
				             : "MiniJV880 Data Card rename not completed",
				         CardName,
				         OldPath,
				         NewName,
				         NewPath,
				         BoolText (bAlreadyActive),
				         BoolText (bSelectedForNextBoot),
				         pCurrentPath,
				         (bRenameOK && bSelectionWriteOK)
				             ? (bSelectedForNextBoot
				                    ? "<p>The file was renamed and current.txt was updated to the new card name.</p>"
				                    : "<p>The file was renamed successfully. current.txt did not need any change.</p>")
				             : (bRolledBackAfterSelectionWriteFail
				                    ? "<p style=\"color:#C00000;\">Updating current.txt failed after rename, so the file rename was rolled back.</p>"
				                    : (!bRenameOK
				                           ? "<p style=\"color:#C00000;\">The rename operation failed. No file selection update was applied.</p>"
				                           : (!bRollbackOK
				                                  ? "<p style=\"color:#C00000;\">The file was renamed, but updating current.txt failed and rollback also failed. Manual repair is required.</p>"
				                                  : "<p style=\"color:#C00000;\">Updating current.txt failed after rename, so the file rename was rolled back.</p>"))),
				         EncodedNewName,
				         (bRenameOK && bSelectionWriteOK && bAlreadyActive)
				             ? "<p><b>Reboot recommended:</b> the current runtime active path matched the old file name before rename.</p>"
				             : "");

			             if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			             {
				            return HTTPInternalServerError;
			             }

			             pBody = StatusPage;
		              }
	                   }
                        }

                        else if (strcmp (pPath, "/cardram-delete") == 0)
                        {
	                   char CardName[256];
	                   bool bHaveNameParam = false;
	                   bool bRequestedNameInvalid = false;
	                   CardName[0] = '\0';

	                   if (pParams != 0 && pParams[0] != '\0')
	                   {
		              const char *pParam = pParams;

		              while (*pParam != '\0')
		              {
		                  const char *pNext = strchr (pParam, '&');
		                  size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

		                  if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
		                  {
			             bHaveNameParam = true;

			             char RawValue[256];
			             size_t nValueLen = nParamLen - 5;
			             if (nValueLen >= sizeof RawValue)
			             {
			                return HTTPInternalServerError;
			             }

			             for (size_t i = 0; i < nValueLen; i++)
			             {
			                char ch = pParam[5 + i];
			                RawValue[i] = ch == '+' ? ' ' : ch;
			             }

			             RawValue[nValueLen] = '\0';

			             if (!URLDecode (RawValue, CardName, sizeof CardName))
			             {
			                bRequestedNameInvalid = true;
			                CardName[0] = '\0';
			             }

			             break;
		                  }

		                  if (pNext == 0)
		                  {
			             break;
		                  }

		                  pParam = pNext + 1;
		              }
	                   }

	                   if (!bHaveNameParam || bRequestedNameInvalid || !IsCardRamBINFileName (CardName))
	                   {
		              return HTTPNotFound;
	                   }

	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   const char *pCurrentPath = MiniJV880_GetCardRamCurrentPath ();
	                   const char *pActivePath = MiniJV880_GetCardRamActivePath ();
	                   bool bUsingCollection = MiniJV880_GetCardRamUsingCollection () != 0;

	                   if (pCollectionDir == 0) pCollectionDir = "";
	                   if (pCurrentPath == 0 || pCurrentPath[0] == '\0') pCurrentPath = "SD:/CARD-RAM/current.txt";
	                   if (pActivePath == 0) pActivePath = "";

	                   char TargetPath[256];
	                   int nPathWritten = snprintf (
		               TargetPath, sizeof TargetPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s/%s" : "SD:/%s/%s",
		               pCollectionDir,
		               CardName);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof TargetPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetExists = false;
	                   char TargetSizeText[64];
	                   if (!GetKernelFileStatusText (
		                   TargetPath,
		                   &bTargetExists,
		                   TargetSizeText,
		                   sizeof TargetSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetRegularFile = false;
	                   struct stat TargetStat;
	                   if (stat (TargetPath, &TargetStat) == 0
	                       && !S_ISDIR (TargetStat.st_mode))
	                   {
		              bTargetRegularFile = true;
	                   }

	                   bool bSelectedForNextBoot = false;
	                   FILE *pCurrentInput = fopen (pCurrentPath, "rb");
	                   if (pCurrentInput != 0)
	                   {
		              char SelectedName[256];
		              size_t nRead = fread (SelectedName, 1, sizeof SelectedName - 1, pCurrentInput);
		              fclose (pCurrentInput);
		              SelectedName[nRead] = '\0';

		              while (nRead > 0
			              && (SelectedName[nRead - 1] == '\n' || SelectedName[nRead - 1] == '\r'))
		              {
			          SelectedName[nRead - 1] = '\0';
			          nRead--;
		              }

		              bSelectedForNextBoot = strcmp (SelectedName, CardName) == 0;
	                   }

	                   bool bAlreadyActive = false;
	                   if (bUsingCollection)
	                   {
		              const char *pLastSlash = strrchr (pActivePath, '/');
		              const char *pActiveName = pLastSlash != 0 ? pLastSlash + 1 : pActivePath;
		              bAlreadyActive = strcmp (pActiveName, CardName) == 0;
	                   }

	                   char EncodedName[512];
	                   if (!URLEncodePathSegment (CardName, EncodedName, sizeof EncodedName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 Data Card delete precheck</title></head>"
		               "<body>"
		               "<h1>MiniJV880 Data Card delete precheck</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<p>This precheck validates the requested Data Card before delete.</p>"
		               "<ul>"
		               "<li>Requested card: %s</li>"
		               "<li>Target path: %s</li>"
		               "<li>Target file present: %s (%s)</li>"
		               "<li>Target path is a regular file: %s</li>"
		               "<li>Selected for next boot: %s</li>"
		               "<li>Already active at runtime: %s</li>"
		               "<li>Selection file: %s</li>"
		               "</ul>"
		               "<p><b>Warning:</b> deleting a card file is irreversible.</p>"
		               "<p><b>Important:</b> delete will be refused if this card is selected for next boot or is already active at runtime.</p>"
		               "<p><a href=\"/cardram-delete-exec?name=%s\" %s>Execute delete now</a></p>"
		               "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
		               "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
		               "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
		               "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
		               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               CardName,
		               TargetPath,
		               BoolText (bTargetExists),
		               KernelStatusHTML (TargetSizeText),
		               BoolText (bTargetRegularFile),
		               BoolText (bSelectedForNextBoot),
		               BoolText (bAlreadyActive),
		               pCurrentPath,
		               EncodedName,
		               ActionLinkStyle (),
		               EncodedName);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }

                        else if (strcmp (pPath, "/cardram-delete-exec") == 0)
                        {
	                   char CardName[256];
	                   bool bHaveNameParam = false;
	                   bool bRequestedNameInvalid = false;
	                   CardName[0] = '\0';

	                   if (pParams != 0 && pParams[0] != '\0')
	                   {
		              const char *pParam = pParams;

		              while (*pParam != '\0')
		              {
		                  const char *pNext = strchr (pParam, '&');
		                  size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

		                  if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
		                  {
			             bHaveNameParam = true;

			             char RawValue[256];
			             size_t nValueLen = nParamLen - 5;
			             if (nValueLen >= sizeof RawValue)
			             {
			                return HTTPInternalServerError;
			             }

			             for (size_t i = 0; i < nValueLen; i++)
			             {
			                char ch = pParam[5 + i];
			                RawValue[i] = ch == '+' ? ' ' : ch;
			             }

			             RawValue[nValueLen] = '\0';

			             if (!URLDecode (RawValue, CardName, sizeof CardName))
			             {
			                bRequestedNameInvalid = true;
			                CardName[0] = '\0';
			             }

			             break;
		                  }

		                  if (pNext == 0)
		                  {
			             break;
		                  }

		                  pParam = pNext + 1;
		              }
	                   }

	                   if (!bHaveNameParam || bRequestedNameInvalid || !IsCardRamBINFileName (CardName))
	                   {
		              return HTTPNotFound;
	                   }

	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   const char *pCurrentPath = MiniJV880_GetCardRamCurrentPath ();
	                   const char *pActivePath = MiniJV880_GetCardRamActivePath ();
	                   bool bUsingCollection = MiniJV880_GetCardRamUsingCollection () != 0;

	                   if (pCollectionDir == 0) pCollectionDir = "";
	                   if (pCurrentPath == 0 || pCurrentPath[0] == '\0') pCurrentPath = "SD:/CARD-RAM/current.txt";
	                   if (pActivePath == 0) pActivePath = "";

	                   char TargetPath[256];
	                   int nPathWritten = snprintf (
		               TargetPath, sizeof TargetPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s/%s" : "SD:/%s/%s",
		               pCollectionDir,
		               CardName);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof TargetPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetExists = false;
	                   char TargetSizeText[64];
	                   if (!GetKernelFileStatusText (
		                   TargetPath,
		                   &bTargetExists,
		                   TargetSizeText,
		                   sizeof TargetSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetRegularFile = false;
	                   struct stat TargetStat;
	                   if (stat (TargetPath, &TargetStat) == 0
	                       && !S_ISDIR (TargetStat.st_mode))
	                   {
		              bTargetRegularFile = true;
	                   }

	                   bool bSelectedForNextBoot = false;
	                   FILE *pCurrentInput = fopen (pCurrentPath, "rb");
	                   if (pCurrentInput != 0)
	                   {
		              char SelectedName[256];
		              size_t nRead = fread (SelectedName, 1, sizeof SelectedName - 1, pCurrentInput);
		              fclose (pCurrentInput);
		              SelectedName[nRead] = '\0';

		              while (nRead > 0
			              && (SelectedName[nRead - 1] == '\n' || SelectedName[nRead - 1] == '\r'))
		              {
			          SelectedName[nRead - 1] = '\0';
			          nRead--;
		              }

		              bSelectedForNextBoot = strcmp (SelectedName, CardName) == 0;
	                   }

	                   bool bAlreadyActive = false;
	                   if (bUsingCollection)
	                   {
		              const char *pLastSlash = strrchr (pActivePath, '/');
		              const char *pActiveName = pLastSlash != 0 ? pLastSlash + 1 : pActivePath;
		              bAlreadyActive = strcmp (pActiveName, CardName) == 0;
	                   }

	                   char EncodedName[512];
	                   if (!URLEncodePathSegment (CardName, EncodedName, sizeof EncodedName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (!bTargetExists || !bTargetRegularFile)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 Data Card delete not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 Data Card delete not executed</h1>"
			          "<p>The requested Data Card file is missing or is not a regular file.</p>"
			          "<ul>"
			          "<li>Requested card: %s</li>"
			          "<li>Target path: %s</li>"
			          "<li>Target present: %s (%s)</li>"
			          "<li>Target is a regular file: %s</li>"
			          "</ul>"
			          "<p>No file was modified.</p>"
			          "<p><a href=\"/cardram-delete?name=%s\">Back to delete precheck</a></p>"
			          "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
			          "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          CardName,
			          TargetPath,
			          BoolText (bTargetExists),
			          KernelStatusHTML (TargetSizeText),
			          BoolText (bTargetRegularFile),
			          EncodedName,
			          EncodedName);

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else if (bSelectedForNextBoot)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 Data Card delete not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 Data Card delete not executed</h1>"
			          "<p>The requested card is currently selected for next boot in current.txt.</p>"
			          "<ul>"
			          "<li>Requested card: %s</li>"
			          "<li>Target path: %s</li>"
			          "<li>Selection file: %s</li>"
			          "</ul>"
			          "<p>Please select another Data Card first, or change the selection explicitly before deleting this file.</p>"
			          "<p>No file was modified.</p>"
			          "<p><a href=\"/cardram-delete?name=%s\">Back to delete precheck</a></p>"
			          "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
			          "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          CardName,
			          TargetPath,
			          pCurrentPath,
			          EncodedName,
			          EncodedName);

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else if (bAlreadyActive)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 Data Card delete not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 Data Card delete not executed</h1>"
			          "<p>The requested card currently matches the active runtime Data Card path.</p>"
			          "<ul>"
			          "<li>Requested card: %s</li>"
			          "<li>Target path: %s</li>"
			          "<li>Current active path: %s</li>"
			          "</ul>"
			          "<p>Please switch to another card and reboot before deleting this file.</p>"
			          "<p>No file was modified.</p>"
			          "<p><a href=\"/cardram-delete?name=%s\">Back to delete precheck</a></p>"
			          "<p><a href=\"/cardram-select?name=%s\">Back to Data Card select precheck</a></p>"
			          "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          CardName,
			          TargetPath,
			          pActivePath,
			          EncodedName,
			          EncodedName);

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              bool bDeleteOK = remove (TargetPath) == 0;

		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>%s</title></head>"
			          "<body>"
			          "<h1%s>%s</h1>"
			          "<ul>"
			          "<li>Requested card: %s</li>"
			          "<li>Deleted path: %s</li>"
			          "<li>Selected for next boot before delete: %s</li>"
			          "<li>Already active at runtime before delete: %s</li>"
			          "</ul>"
			          "%s"
			          "<p><a href=\"/cardram-list\">Back to MiniJV880 Data Card collection list</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          bDeleteOK
			              ? "MiniJV880 Data Card delete completed"
			              : "MiniJV880 Data Card delete not completed",
			          bDeleteOK ? "" : " style=\"color:#C00000;\"",
			          bDeleteOK
			              ? "MiniJV880 Data Card delete completed"
			              : "MiniJV880 Data Card delete not completed",
			          CardName,
			          TargetPath,
			          BoolText (bSelectedForNextBoot),
			          BoolText (bAlreadyActive),
			          bDeleteOK
			              ? "<p>The requested Data Card file was deleted successfully.</p>"
			              : "<p style=\"color:#C00000;\">The delete operation failed.</p>");

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
                        }

                        else if (strcmp (pPath, "/cardram-select-exec") == 0)
                        {
	                   char CardName[256];
	                   bool bHaveNameParam = false;
	                   bool bRequestedNameInvalid = false;
	                   CardName[0] = '\0';

	                   if (pParams != 0 && pParams[0] != '\0')
	                   {
		              const char *pParam = pParams;

		              while (*pParam != '\0')
		              {
		                  const char *pNext = strchr (pParam, '&');
		                  size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

		                  if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
		                  {
			             bHaveNameParam = true;

			             char RawValue[256];
			             size_t nValueLen = nParamLen - 5;
			             if (nValueLen >= sizeof RawValue)
			             {
			                return HTTPInternalServerError;
			             }

			             for (size_t i = 0; i < nValueLen; i++)
			             {
			                char ch = pParam[5 + i];
			                RawValue[i] = ch == '+' ? ' ' : ch;
			             }

			             RawValue[nValueLen] = '\0';

			             if (!URLDecode (RawValue, CardName, sizeof CardName))
			             {
			                bRequestedNameInvalid = true;
			                CardName[0] = '\0';
			             }

			             break;
		                  }

		                  if (pNext == 0)
		                  {
			             break;
		                  }

		                  pParam = pNext + 1;
		              }
	                   }

	                   if (!bHaveNameParam || bRequestedNameInvalid || !IsCardRamBINFileName (CardName))
	                   {
		              return HTTPNotFound;
	                   }

	                   char EncodedName[512];
	                   if (!URLEncodePathSegment (CardName, EncodedName, sizeof EncodedName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   if (pCollectionDir == 0) pCollectionDir = "";

	                   char TargetPath[256];
	                   int nPathWritten = snprintf (
		               TargetPath, sizeof TargetPath,
		               strncmp (pCollectionDir, "SD:", 3) == 0 ? "%s/%s" : "SD:/%s/%s",
		               pCollectionDir,
		               CardName);

	                   if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof TargetPath)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bTargetSizeOK = false;
	                   struct stat TargetStat;
	                   if (stat (TargetPath, &TargetStat) == 0
	                       && !S_ISDIR (TargetStat.st_mode)
	                       && TargetStat.st_size == 32768)
	                   {
		              bTargetSizeOK = true;
	                   }

	                   if (!bTargetSizeOK)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 Data Card select not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 Data Card select not executed</h1>"
			          "<p>The selected Data Card is missing or has an invalid size.</p>"
			          "<ul>"
			          "<li>Requested card: %s</li>"
			          "<li>Target path: %s</li>"
			          "<li>Required size: 32768 bytes</li>"
			          "</ul>"
			          "<p>No file was modified.</p>"
			          "<p><a href=\"/cardram-select?name=%s\">Back to this card precheck</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          CardName,
			          TargetPath,
			          EncodedName);

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			          return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
                        }
	                   else
	                   {
		              MiniJV880_FlushCardRamIfNeededNow ();

		              bool bWriteOK = WriteCardRamCurrentSelectionFile (CardName);

		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 Data Card selection updated</title></head>"
			          "<body>"
			          "<h1>MiniJV880 Data Card selection updated</h1>"
			          "<p>The active Data Card was flushed before changing the selection.</p>"
			          "<p>The selection file has %s.</p>"
			          "<ul>"
			          "<li>Selected card for next boot: %s</li>"
			          "<li>Selection file: SD:/CARD-RAM/current.txt</li>"
			          "<li>Target path: %s</li>"
			          "</ul>"
			          "%s"
			          "<p><a href=\"/cardram-select?name=%s\">Back to this card precheck</a></p>"
			          "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
			          "<p><a href=\"/kernel-reboot\">Open reboot page</a></p>"
			          "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
			          "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          bWriteOK ? "been updated" : "NOT been updated",
			          CardName,
			          TargetPath,
			          bWriteOK
			              ? "<p><b>Reboot required:</b> the selected card will become active after reboot.</p>"
			              : "<p style=\"color:#C00000;\"><b>Error:</b> current.txt could not be written. No card selection change was completed.</p>",
			          EncodedName);

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			          return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
                        }

                        else if (strcmp (pPath, "/cardram-select-exec-test") == 0)
                        {
	                   char CardName[256];
	                   snprintf (CardName, sizeof CardName, "TestCard.bin");

	                   if (strcmp (pPath, "/cardram-select-exec") == 0)
	                   {
		              bool bHaveNameParam = false;
		              bool bRequestedNameInvalid = false;
		              CardName[0] = '\0';

		              if (pParams != 0 && pParams[0] != '\0')
		              {
		                  const char *pParam = pParams;

		                  while (*pParam != '\0')
		                  {
			             const char *pNext = strchr (pParam, '&');
			             size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

			             if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
			             {
					bHaveNameParam = true;

					char RawValue[256];
					size_t nValueLen = nParamLen - 5;
					if (nValueLen >= sizeof RawValue)
					{
					    return HTTPInternalServerError;
					}

					for (size_t i = 0; i < nValueLen; i++)
					{
					    char ch = pParam[5 + i];
					    RawValue[i] = ch == '+' ? ' ' : ch;
					}

					RawValue[nValueLen] = '\0';

					if (!URLDecode (RawValue, CardName, sizeof CardName))
					{
					    bRequestedNameInvalid = true;
					    CardName[0] = '\0';
					}

					break;
			             }

			             if (pNext == 0)
			             {
					break;
			             }

			             pParam = pNext + 1;
		                  }
		              }

		              if (!bHaveNameParam || bRequestedNameInvalid || !IsCardRamBINFileName (CardName))
		              {
		                  return HTTPNotFound;
		              }
	                   }

	                   const char *pCardName = CardName;

	                   char EncodedName[512];
	                   if (!URLEncodePathSegment (pCardName, EncodedName, sizeof EncodedName))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 Data Card select execution preview</title></head>"
		               "<body>"
		               "<h1>MiniJV880 Data Card select execution preview</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<p>Requested card: %s</p>"
		               "<p>The real selection execution will later do:</p>"
		               "<ol>"
		               "<li>Flush the currently active Data Card.</li>"
		               "<li>Write SD:/CARD-RAM/current.txt with this selected card name.</li>"
		               "<li>Ask the user to reboot MiniJV880.</li>"
		               "</ol>"
		               "<p>Execution is not enabled yet.</p>"
		               "<p><a href=\"/cardram-select?name=%s\">Back to this card precheck</a></p>"
		               "<p><a href=\"/cardram\">Back to Data Card RAM status</a></p>"
		               "<p><a href=\"/maintenance\">Back to maintenance overview</a></p>"
		               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               pCardName,
		               EncodedName);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }

                        else if (strcmp (pPath, "/cardram.txt") == 0)
                        {
	                   const char *pActivePath = MiniJV880_GetCardRamActivePath ();
	                   const char *pTmpPath = MiniJV880_GetCardRamTmpPath ();
	                   const char *pCollectionDir = MiniJV880_GetCardRamCollectionDir ();
	                   const char *pCurrentPath = MiniJV880_GetCardRamCurrentPath ();
	                   const char *pLegacyPath = MiniJV880_GetCardRamLegacyPath ();
	                   bool bUsingCollection = MiniJV880_GetCardRamUsingCollection () != 0;

	                   if (pActivePath == 0) pActivePath = "";
	                   if (pTmpPath == 0) pTmpPath = "";
	                   if (pCollectionDir == 0) pCollectionDir = "";
	                   if (pCurrentPath == 0) pCurrentPath = "";
	                   if (pLegacyPath == 0) pLegacyPath = "";

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "MiniJV880 Data Card RAM status\n"
		               "Read-only runtime diagnostics. No file is modified here.\n"
		               "No SD file scan or digest is performed on this endpoint.\n"
		               "\n"
		               "Active mode: %s\n"
		               "Using collection: %s\n"
		               "Active path: %s\n"
		               "Temporary path: %s\n"
		               "Collection directory: %s\n"
		               "Selection file: %s\n"
		               "Legacy path: %s\n",
		               bUsingCollection ? "collection" : "legacy",
		               bUsingCollection ? "yes" : "no",
		               pActivePath,
		               pTmpPath,
		               pCollectionDir,
		               pCurrentPath,
		               pLegacyPath);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   size_t nBodyLength = strlen (StatusPage);
	                   if (nBodyLength > *pLength)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   memcpy (pBuffer, StatusPage, nBodyLength);
	                   *pLength = (unsigned) nBodyLength;
	                   *ppContentType = "text/plain; charset=iso-8859-1";
	                   return HTTPOK;
                        }


                        else if (strcmp (pPath, "/status.txt") == 0)
                        {
	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "MiniJV880 network status\n"
		               "Hostname: %s\n"
		               "Port: %u\n"
		               "DHCP: %s\n"
		               "Configured IP: %s\n"
		               "Configured mask: %s\n"
		               "Configured gateway: %s\n"
		               "Expose PN-JV80: %s\n"
		               "Expose roms: %s\n"
		               "Write enabled: %s\n",
		               m_Config.m_HostName.empty () ? "minijv880" : m_Config.m_HostName.c_str (),
		               m_Config.m_nPort,
		               m_Config.m_bDHCP ? "yes" : "no",
		               m_Config.m_IP.empty () ? "(dhcp)" : m_Config.m_IP.c_str (),
		               m_Config.m_Mask.empty () ? "(dhcp)" : m_Config.m_Mask.c_str (),
		               m_Config.m_Gateway.empty () ? "(dhcp)" : m_Config.m_Gateway.c_str (),
		               m_Config.m_bExposePNJV80 ? "yes" : "no",
		               m_Config.m_bExposeRoms ? "yes" : "no",
		               m_Config.m_bWriteEnable ? "yes" : "no");

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   size_t nBodyLength = strlen (StatusPage);
	                   if (nBodyLength > *pLength)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   memcpy (pBuffer, StatusPage, nBodyLength);
	                   *pLength = (unsigned) nBodyLength;
	                   *ppContentType = "text/plain; charset=iso-8859-1";
	                   return HTTPOK;
                        }

                        else if (strcmp (pPath, "/ini-status.txt") == 0)
                        {
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIBackupExists = false;
	                   bool bINIIdentical = false;
	                   unsigned nINIFirstDifferingLine = 0;
	                   char INISizeText[64];
	                   char INIStageSizeText[64];
	                   char INIBackupSizeText[64];
	                   char INIDigestText[32];
	                   char INIStageDigestText[32];
	                   char INIBackupDigestText[32];
	                   char INIIdenticalText[16];
	                   char INIFirstDiffText[32];

	                   if (!GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIBackupPath,
	                           &bINIBackupExists,
	                           INIBackupSizeText,
	                           sizeof INIBackupSizeText)
	                       || !GetKernelFileDigestText (
	                           kINIActivePath,
	                           INIDigestText,
	                           sizeof INIDigestText)
	                       || !GetKernelFileDigestText (
	                           kINIStagePath,
	                           INIStageDigestText,
	                           sizeof INIStageDigestText)
	                       || !GetKernelFileDigestText (
	                           kINIBackupPath,
	                           INIBackupDigestText,
	                           sizeof INIBackupDigestText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (bINIExists && bINIStageExists)
	                   {
		              if (!GetFirstDifferingLine (
		                      kINIActivePath,
		                      kINIStagePath,
		                      &bINIIdentical,
		                      &nINIFirstDifferingLine))
		              {
			             return HTTPInternalServerError;
		              }

		              int nFlagWritten = snprintf (
			          INIIdenticalText, sizeof INIIdenticalText,
			          "%s",
			          bINIIdentical ? "yes" : "no");

		              if (nFlagWritten < 0 || (unsigned) nFlagWritten >= sizeof INIIdenticalText)
		              {
			             return HTTPInternalServerError;
		              }

		              int nDiffWritten = snprintf (
			          INIFirstDiffText, sizeof INIFirstDiffText,
			          "%s",
			          bINIIdentical ? "none" : "");

		              if (nDiffWritten < 0 || (unsigned) nDiffWritten >= sizeof INIFirstDiffText)
		              {
			             return HTTPInternalServerError;
		              }

		              if (!bINIIdentical)
		              {
			             nDiffWritten = snprintf (
				         INIFirstDiffText, sizeof INIFirstDiffText,
				         "%u",
				         nINIFirstDifferingLine);

			             if (nDiffWritten < 0 || (unsigned) nDiffWritten >= sizeof INIFirstDiffText)
			             {
				            return HTTPInternalServerError;
			             }
		              }
	                   }
	                   else
	                   {
		              int nFlagWritten = snprintf (
			          INIIdenticalText, sizeof INIIdenticalText,
			          "(not available)");

		              if (nFlagWritten < 0 || (unsigned) nFlagWritten >= sizeof INIIdenticalText)
		              {
			             return HTTPInternalServerError;
		              }

		              int nDiffWritten = snprintf (
			          INIFirstDiffText, sizeof INIFirstDiffText,
			          "(not available)");

		              if (nDiffWritten < 0 || (unsigned) nDiffWritten >= sizeof INIFirstDiffText)
		              {
			             return HTTPInternalServerError;
		              }
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "MiniJV880 INI status\n"
		               "Active exists: %s\n"
		               "Active size: %s\n"
		               "Active digest: %s\n"
		               "Staged exists: %s\n"
		               "Staged size: %s\n"
		               "Staged digest: %s\n"
		               "Backup exists: %s\n"
		               "Backup size: %s\n"
		               "Backup digest: %s\n"
		               "Identical now: %s\n"
		               "First differing line: %s\n",
		               bINIExists ? "yes" : "no",
		               INISizeText,
		               INIDigestText,
		               bINIStageExists ? "yes" : "no",
		               INIStageSizeText,
		               INIStageDigestText,
		               bINIBackupExists ? "yes" : "no",
		               INIBackupSizeText,
		               INIBackupDigestText,
		               INIIdenticalText,
		               INIFirstDiffText);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   size_t nBodyLength = strlen (StatusPage);
	                   if (nBodyLength > *pLength)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   memcpy (pBuffer, StatusPage, nBodyLength);
	                   *pLength = (unsigned) nBodyLength;
	                   *ppContentType = "text/plain; charset=iso-8859-1";
	                   return HTTPOK;
                        }

	                        else if (strcmp (pPath, "/boot-layout.txt") == 0)
	                        {
		                   TBootLayoutInfo BootLayout;
		                   DetectBootLayout (&BootLayout);

		                   int nWritten = snprintf (
		                       StatusPage, sizeof StatusPage,
		                       "MiniJV880 boot layout\n"
		                       "boot_layout=%s\n"
		                       "managed_kernel=MiniJV880\n"
		                       "managed_kernel_active_path=%s\n"
		                       "managed_kernel_stage_path=%s\n"
		                       "managed_kernel_backup_path=%s\n"
		                       "singleboot_kernel_present=%s\n"
		                       "dualboot_minijv880_kernel_present=%s\n"
		                       "minidexed_kernel_present=%s\n"
		                       "minidexed_kernel_mode=read-only-detected-only\n",
		                       BootLayout.LayoutName,
		                       BootLayout.ManagedKernelActivePath,
		                       BootLayout.ManagedKernelStagePath,
		                       BootLayout.ManagedKernelBackupPath,
		                       BootLayout.SinglebootKernelPresent ? "yes" : "no",
		                       BootLayout.DualbootMiniJV880KernelPresent ? "yes" : "no",
		                       BootLayout.MiniDexedKernelPresent ? "yes" : "no");

		                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		                   {
		                      return HTTPInternalServerError;
		                   }

		                   size_t nBodyLength = strlen (StatusPage);
		                   if (nBodyLength > *pLength)
		                   {
		                      return HTTPInternalServerError;
		                   }

		                   memcpy (pBuffer, StatusPage, nBodyLength);
		                   *pLength = (unsigned) nBodyLength;
		                   *ppContentType = "text/plain; charset=iso-8859-1";
		                   return HTTPOK;
	                        }

	                        else if (strcmp (pPath, "/kernel-status.txt") == 0)
	                        {
		                   bool bKernelExists = false;
		                   bool bStageExists = false;
		                   bool bBackupExists = false;
		                   char KernelSizeText[64];
		                   char StageSizeText[64];
		                   char BackupSizeText[64];
		                   char KernelDigestText[32];
		                   char StageDigestText[32];
		                   char BackupDigestText[32];

		                   TBootLayoutInfo BootLayout;
		                   DetectBootLayout (&BootLayout);

		                   if (!GetKernelFileStatusText (
		                           BootLayout.ManagedKernelActivePath,
		                           &bKernelExists,
		                           KernelSizeText,
		                           sizeof KernelSizeText)
		                       || !GetKernelFileStatusText (
		                           BootLayout.ManagedKernelStagePath,
		                           &bStageExists,
		                           StageSizeText,
		                           sizeof StageSizeText)
		                       || !GetKernelFileStatusText (
		                           BootLayout.ManagedKernelBackupPath,
		                           &bBackupExists,
		                           BackupSizeText,
		                           sizeof BackupSizeText)
		                       || !GetKernelFileDigestText (
		                           BootLayout.ManagedKernelActivePath,
		                           KernelDigestText,
		                           sizeof KernelDigestText)
		                       || !GetKernelFileDigestText (
		                           BootLayout.ManagedKernelStagePath,
		                           StageDigestText,
		                           sizeof StageDigestText)
		                       || !GetKernelFileDigestText (
		                           BootLayout.ManagedKernelBackupPath,
		                           BackupDigestText,
		                           sizeof BackupDigestText))
		                   {
			              return HTTPInternalServerError;
		                   }

		                   int nWritten = snprintf (
			               StatusPage, sizeof StatusPage,
			               "MiniJV880 kernel status\n"
			               "Boot layout: %s\n"
			               "Managed kernel: MiniJV880\n"
			               "Active path: %s\n"
			               "Active exists: %s\n"
			               "Active size: %s\n"
			               "Active digest: %s\n"
			               "Staged path: %s\n"
			               "Staged exists: %s\n"
			               "Staged size: %s\n"
			               "Staged digest: %s\n"
			               "Backup path: %s\n"
			               "Backup exists: %s\n"
			               "Backup size: %s\n"
			               "Backup digest: %s\n"
			               "MiniDexed kernel present: %s\n"
			               "MiniDexed kernel mode: read-only-detected-only\n",
			               BootLayout.LayoutName,
			               BootLayout.ManagedKernelActivePath,
			               bKernelExists ? "yes" : "no",
			               KernelSizeText,
			               KernelDigestText,
			               BootLayout.ManagedKernelStagePath,
			               bStageExists ? "yes" : "no",
			               StageSizeText,
			               StageDigestText,
			               BootLayout.ManagedKernelBackupPath,
			               bBackupExists ? "yes" : "no",
			               BackupSizeText,
			               BackupDigestText,
			               BootLayout.MiniDexedKernelPresent ? "yes" : "no");

		                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		                   {
			              return HTTPInternalServerError;
		                   }

		                   size_t nBodyLength = strlen (StatusPage);
		                   if (nBodyLength > *pLength)
		                   {
			              return HTTPInternalServerError;
		                   }

		                   memcpy (pBuffer, StatusPage, nBodyLength);
		                   *pLength = (unsigned) nBodyLength;
		                   *ppContentType = "text/plain; charset=iso-8859-1";
		                   return HTTPOK;
	                        }


                        else if (strcmp (pPath, "/maintenance") == 0)
                        {
	                   bool bKernelExists = false;
	                   bool bKernelStageExists = false;
	                   bool bKernelBackupExists = false;
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIBackupExists = false;
	                   char KernelSizeText[64];
	                   char KernelStageSizeText[64];
	                   char KernelBackupSizeText[64];
	                   char INISizeText[64];
	                   char INIStageSizeText[64];
	                   char INIBackupSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           kKernelActivePath,
	                           &bKernelExists,
	                           KernelSizeText,
	                           sizeof KernelSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelStagePath,
	                           &bKernelStageExists,
	                           KernelStageSizeText,
	                           sizeof KernelStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelBackupPath,
	                           &bKernelBackupExists,
	                           KernelBackupSizeText,
	                           sizeof KernelBackupSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIBackupPath,
	                           &bINIBackupExists,
	                           INIBackupSizeText,
	                           sizeof INIBackupSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 maintenance overview</title></head>"
		               "<body>"
		               "<h1>MiniJV880 maintenance overview</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<h2>Kernel files</h2>"
		               "<ul>"
		               "<li>Current active kernel present: %s (%s)</li>"
		               "<li>Current staged kernel present: %s (%s)</li>"
		               "<li>Current backup kernel present: %s (%s)</li>"
		               "</ul>"
		               "<h2>INI files</h2>"
		               "<ul>"
		               "<li>Current active INI present: %s (%s)</li>"
		               "<li>Current staged INI present: %s (%s)</li>"
		               "<li>Current backup INI present: %s (%s)</li>"
		               "</ul>"
		               "<p style=\"margin:0;\"><a href=\"/cardram\">Open Data Card RAM status page</a></p>"
		               "<p style=\"margin:0.8em 0 0 0;\"><a href=\"/kernel-status\">Open kernel status page</a></p>"
		               "<p style=\"margin:0;\"><a href=\"/kernel-activate\">Open kernel activate precheck page</a></p>"
		               "<p style=\"margin:0;\"><a href=\"/kernel-reboot\">Open kernel reboot page</a></p>"
		               "<p style=\"margin:0.8em 0 0 0;\"><a href=\"/ini-status\">Open INI status page</a></p>"
		               "<p style=\"margin:0;\"><a href=\"/ini-apply\">Open INI apply precheck page</a></p>"
		               "<p style=\"margin:0.8em 0 0 0;\"><a href=\"/status\">Back to status</a></p>"
		               "<p style=\"margin:0;\"><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               BoolText (bKernelExists),
		               KernelStatusInnerHTML (KernelSizeText),
		               BoolText (bKernelStageExists),
		               KernelStatusInnerHTML (KernelStageSizeText),
		               BoolText (bKernelBackupExists),
		               KernelStatusInnerHTML (KernelBackupSizeText),
		               BoolText (bINIExists),
		               KernelStatusInnerHTML (INISizeText),
		               BoolText (bINIStageExists),
		               KernelStatusInnerHTML (INIStageSizeText),
		               BoolText (bINIBackupExists),
		               KernelStatusInnerHTML (INIBackupSizeText));

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }
                      
                        else if (strcmp (pPath, "/ini") == 0)
                        {
	                    int nWritten = snprintf (
		                StatusPage, sizeof StatusPage,
		                "<html>"
		                "<head><title>MiniJV880 INI home</title></head>"
		                "<body>"
		                "<h1>MiniJV880 INI home</h1>"
		                "<p>This area will manage minijv880.ini using explicit and separate steps.</p>"
		                "<ul>"
		                "<li><a href=\"/ini-status\">Open INI status page</a></li>"
		                "<li><a href=\"/ini-download\">Download current INI file</a></li>"
		                "<li><a href=\"/ini-backup-download\">Download current INI backup file</a></li>"
		                "<li><a href=\"/ini-upload\">Open INI staging upload page</a></li>"
		                "<li><a href=\"/ini-validate\">Open INI validator precheck page</a></li>"
		                "<li><a href=\"/ini-apply\">Open INI apply precheck page</a></li>"
		                "<li><a href=\"/ini-backup-delete\">Delete current INI backup now</a></li>"
		                "<li>Explicit reboot remains separate</li>"
		                "</ul>"
		                "<p>This is a minimal entry page only. No INI file is modified here.</p>"
		                "<p><a href=\"/status\">Back to status</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
		                "</body>"
		                "</html>");

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }
                        else if (strcmp (pPath, "/ini-status") == 0)
                        {
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIBackupExists = false;
	                   bool bINIIdentical = false;
	                   unsigned nINIFirstDifferingLine = 0;
	                   char INISizeText[64];
	                   char INIStageSizeText[64];
	                   char INIBackupSizeText[64];
	                   char INIDigestText[32];
	                   char INIStageDigestText[32];
	                   char INIBackupDigestText[32];
	                   char INIDiffSummaryText[192];
	                   char INIDiffLinkText[96];

	                   if (!GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIBackupPath,
	                           &bINIBackupExists,
	                           INIBackupSizeText,
	                           sizeof INIBackupSizeText)
	                       || !GetKernelFileDigestText (
	                           kINIActivePath,
	                           INIDigestText,
	                           sizeof INIDigestText)
	                       || !GetKernelFileDigestText (
	                           kINIStagePath,
	                           INIStageDigestText,
	                           sizeof INIStageDigestText)
	                       || !GetKernelFileDigestText (
	                           kINIBackupPath,
	                           INIBackupDigestText,
	                           sizeof INIBackupDigestText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   INIDiffSummaryText[0] = '\0';
	                   INIDiffLinkText[0] = '\0';

	                   if (bINIExists && bINIStageExists)
	                   {
		              if (!GetFirstDifferingLine (
		                      kINIActivePath,
		                      kINIStagePath,
		                      &bINIIdentical,
		                      &nINIFirstDifferingLine))
		              {
			             return HTTPInternalServerError;
		              }

		              if (bINIIdentical)
		              {
			             int nSummaryWritten = snprintf (
				         INIDiffSummaryText, sizeof INIDiffSummaryText,
				         "<p style=\"color:#0047CC; font-weight:bold;\">Active and staged INI are currently identical.</p>");

			             if (nSummaryWritten < 0 || (unsigned) nSummaryWritten >= sizeof INIDiffSummaryText)
			             {
				            return HTTPInternalServerError;
			             }
		              }
		              else
		              {
			             int nSummaryWritten = snprintf (
				         INIDiffSummaryText, sizeof INIDiffSummaryText,
				         "<p style=\"color:#C00000; font-weight:bold;\">Active and staged INI are currently different. First differing line: %u.</p>",
				         nINIFirstDifferingLine);

			             if (nSummaryWritten < 0 || (unsigned) nSummaryWritten >= sizeof INIDiffSummaryText)
			             {
				            return HTTPInternalServerError;
			             }

			             int nLinkWritten = snprintf (
				         INIDiffLinkText, sizeof INIDiffLinkText,
				         "<p><a href=\"/ini-diff\">Open INI diff preview page</a></p>");

			             if (nLinkWritten < 0 || (unsigned) nLinkWritten >= sizeof INIDiffLinkText)
			             {
				            return HTTPInternalServerError;
			             }
		              }
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 INI status</title></head>"
		               "<body>"
		               "<h1>MiniJV880 INI status</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<ul>"
		               "<li>SD:/minijv880.ini - exists: %s - size: %s - digest: %s</li>"
		               "<li>SD:/minijv880.ini.new - exists: %s - size: %s - digest: %s</li>"
		               "<li>SD:/minijv880.ini.bak - exists: %s - size: %s - digest: %s</li>"
		               "</ul>"
			               "<p>If two files have the same size, the digest helps determine whether their contents are actually different.</p>"
			               "%s"
			               "%s"
		               "<p><a href=\"/ini-status.txt\">Open plain text INI status endpoint</a></p>"
		               "<p><a href=\"/ini-download\">Download current INI file</a></p>"
		               "<p><a href=\"/ini-backup-download\">Download current INI backup file</a></p>"
		               "<p><a href=\"/ini-upload\">Open INI staging upload page</a></p>"
		               "<p><a href=\"/ini-validate\">Open INI validator precheck page</a></p>"
		               "<p><a href=\"/ini-apply\">Open INI apply precheck page</a></p>"
		               "<p><a href=\"/ini-backup-delete\">Delete current INI backup now</a></p>"
		               "<p><a href=\"/ini\">Back to INI home</a></p>"
		               "</body>"
		               "</html>",
			               BoolText (bINIExists),
			               KernelStatusInnerHTML (INISizeText),
			               KernelStatusHTML (INIDigestText),
			               BoolText (bINIStageExists),
			               KernelStatusInnerHTML (INIStageSizeText),
			               KernelStatusHTML (INIStageDigestText),
			               BoolText (bINIBackupExists),
			               KernelStatusInnerHTML (INIBackupSizeText),
			               KernelStatusHTML (INIBackupDigestText),
			               INIDiffSummaryText,
			               INIDiffLinkText);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }
                      
                        else if (strcmp (pPath, "/ini-upload") == 0)
                        {
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIBackupExists = false;
	                   char INISizeText[64];
	                   char INIStageSizeText[64];
	                   char INIBackupSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIBackupPath,
	                           &bINIBackupExists,
	                           INIBackupSizeText,
	                           sizeof INIBackupSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 INI staging upload</title></head>"
		               "<body>"
		               "<h1>MiniJV880 INI staging upload</h1>"
		               "<p>Use a TFTP client to upload a local INI file with the remote file name <b>minijv880.ini</b>.</p>"
		               "<ul>"
		               "<li>Current active INI present: %s (%s)</li>"
		               "<li>Current staged INI present: %s (%s)</li>"
		               "<li>Current backup INI present: %s (%s)</li>"
		               "<li>TFTP remote file name to use: <b>minijv880.ini</b></li>"
		               "<li>Staging target on SD: <b>SD:/minijv880.ini.new</b></li>"
		               "<li>Current active INI modified by TFTP upload: %s</li>"
		               "<li>TFTP staging upload available now: %s</li>"
		               "<li>Explicit reboot remains separate</li>"
		               "</ul>"
		               "%s"
		               "<p>After the TFTP upload completes, reopen the INI status page to verify that the staged INI file is present.</p>"
		               "<p><a href=\"/ini-status\">Back to INI status</a></p>"
		               "<p><a href=\"/ini\">Back to INI home</a></p>"
                               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               BoolText (bINIExists),
		               KernelStatusInnerHTML (INISizeText),
		               BoolText (bINIStageExists),
		               KernelStatusInnerHTML (INIStageSizeText),
		               BoolText (bINIBackupExists),
		               KernelStatusInnerHTML (INIBackupSizeText),
		               BoolText (false),
		               BoolText (true),
		               bINIStageExists
		                   ? "<p style=\"color:#C00000; font-weight:bold;\">A staged INI file is already present. A new TFTP upload of minijv880.ini will replace it explicitly.</p>"
		                   : "");

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }

                        else if (strcmp (pPath, "/ini-validate") == 0)
                        {
	                   bool bINIStageExists = false;
	                   char INIStageSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (!bINIStageExists)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 staged INI validation not available</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 staged INI validation not available</h1>"
			          "<p style=\"color:#C00000; font-weight:bold;\">No staged INI file is currently present, so validation cannot be performed.</p>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>");

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              FILE *pInput = fopen (kINIStagePath, "rb");
		              if (pInput == 0)
		              {
			             return HTTPInternalServerError;
		              }

		              unsigned nTotalLines = 0;
		              unsigned nCandidateLines = 0;
		              unsigned nMalformedLines = 0;
		              unsigned nFirstMalformedLine = 0;
		              bool bFileEmpty = true;
		              char Line[512];

		              while (fgets (Line, sizeof Line, pInput) != 0)
		              {
			             bFileEmpty = false;
			             nTotalLines++;

			             bool bCompleteLine =
			                    strchr (Line, '\n') != 0
			                 || feof (pInput);

			             if (!bCompleteLine)
			             {
				            int ch;
				            while ((ch = fgetc (pInput)) != EOF && ch != '\n')
				            {
				            }

				            nMalformedLines++;
				            if (nFirstMalformedLine == 0)
				            {
					       nFirstMalformedLine = nTotalLines;
				            }
				            continue;
			             }

			             size_t nLen = strlen (Line);
			             while (nLen > 0
			                 && (Line[nLen-1] == '\n' || Line[nLen-1] == '\r'))
			             {
				            Line[--nLen] = '\0';
			             }

			             char *pScan = Line;
			             while (*pScan == ' ' || *pScan == '\t')
			             {
				            pScan++;
			             }

			             if (*pScan == '\0' || *pScan == ';' || *pScan == '#')
			             {
				            continue;
			             }

			             nCandidateLines++;

			             char *pEquals = strchr (pScan, '=');
			             if (pEquals == 0 || pEquals == pScan || pEquals[1] == '\0')
			             {
				            nMalformedLines++;
				            if (nFirstMalformedLine == 0)
				            {
					       nFirstMalformedLine = nTotalLines;
				            }
			             }
		              }

		              if (ferror (pInput))
		              {
			             fclose (pInput);
			             return HTTPInternalServerError;
		              }

		              if (fclose (pInput) != 0)
		              {
			             return HTTPInternalServerError;
		              }

			              char MalformedLinesText[96];
			              int nMalformedWritten = snprintf (
				          MalformedLinesText, sizeof MalformedLinesText,
				          "%u",
				          nMalformedLines);

			              if (nMalformedWritten < 0 || (unsigned) nMalformedWritten >= sizeof MalformedLinesText)
			              {
				             return HTTPInternalServerError;
			              }

			              if (nMalformedLines != 0)
			              {
				             nMalformedWritten = snprintf (
					         MalformedLinesText, sizeof MalformedLinesText,
					         "<span style=\"color:#C00000; font-weight:bold;\">%u</span>",
					         nMalformedLines);

				             if (nMalformedWritten < 0 || (unsigned) nMalformedWritten >= sizeof MalformedLinesText)
				             {
					            return HTTPInternalServerError;
				             }
			              }

		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 staged INI validation</title></head>"
			          "<body>"
			          "<h1>MiniJV880 staged INI validation</h1>"
			          "<p>This page is read-only. No file is modified here.</p>"
			          "<p>This is a basic structural validation only. INI key semantics are not checked here.</p>"
			          "<ul>"
			          "<li>Current staged INI present: %s (%s)</li>"
			          "<li>File empty: %s</li>"
			          "<li>Total lines: %u</li>"
			          "<li>Candidate setting lines: %u</li>"
				  "<li>Malformed lines: %s</li>"
			          "<li>First malformed line: %s</li>"
			          "</ul>"
			          "%s"
			          "<p><a href=\"/ini-upload\">Back to INI staging upload page</a></p>"
			          "<p><a href=\"/ini-apply\">Open INI apply precheck page</a></p>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          BoolText (bINIStageExists),
			          KernelStatusInnerHTML (INIStageSizeText),
			          BoolText (bFileEmpty),
			          nTotalLines,
			          nCandidateLines,
				  MalformedLinesText,
			          nFirstMalformedLine == 0 ? "none" : KernelStatusInnerHTML (""),
			          nMalformedLines == 0
			              ? "<p style=\"color:#0047CC; font-weight:bold;\">No malformed setting lines were found by this basic structural check.</p>"
			              : "<p style=\"color:#C00000; font-weight:bold;\">One or more malformed setting lines were found. Review the staged INI file before apply.</p>");

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              if (nFirstMalformedLine != 0)
		              {
			             char FirstMalformedText[96];
			             int nLineWritten = snprintf (
				         FirstMalformedText, sizeof FirstMalformedText,
				         "<span style=\"color:#C00000; font-weight:bold;\">%u</span>",
				         nFirstMalformedLine);

			             if (nLineWritten < 0 || (unsigned) nLineWritten >= sizeof FirstMalformedText)
			             {
				            return HTTPInternalServerError;
			             }

			             char *pNeedle = strstr (StatusPage, "First malformed line: ");
			             if (pNeedle == 0)
			             {
				            return HTTPInternalServerError;
			             }

			             char *pValueStart = pNeedle + strlen ("First malformed line: ");
			             char *pValueEnd = strstr (pValueStart, "</li>");
			             if (pValueEnd == 0)
			             {
				            return HTTPInternalServerError;
			             }

			             char Tail[2048];
			             size_t nTailLen = strlen (pValueEnd);
			             if (nTailLen >= sizeof Tail)
			             {
				            return HTTPInternalServerError;
			             }

			             memcpy (Tail, pValueEnd, nTailLen + 1);

			             if ((size_t) (pValueStart - StatusPage) + strlen (FirstMalformedText) + nTailLen >= sizeof StatusPage)
			             {
				            return HTTPInternalServerError;
			             }

			             strcpy (pValueStart, FirstMalformedText);
			             strcpy (pValueStart + strlen (FirstMalformedText), Tail);
		              }

		              pBody = StatusPage;
	                   }
                        }
                      
                        else if (strcmp (pPath, "/ini-diff") == 0)
                        {
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIIdentical = false;
	                   unsigned nINIFirstDifferingLine = 0;
	                   char INISizeText[64];
	                   char INIStageSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (!bINIExists || !bINIStageExists)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 INI diff preview not available</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 INI diff preview not available</h1>"
			          "<p style=\"color:#C00000; font-weight:bold;\">Both the active INI file and the staged INI file must be present before a diff preview can be shown.</p>"
			          "<ul>"
			          "<li>Current active INI present: %s (%s)</li>"
			          "<li>Current staged INI present: %s (%s)</li>"
			          "</ul>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          BoolText (bINIExists),
			          KernelStatusInnerHTML (INISizeText),
			          BoolText (bINIStageExists),
			          KernelStatusInnerHTML (INIStageSizeText));

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              if (!GetFirstDifferingLine (
		                      kINIActivePath,
		                      kINIStagePath,
		                      &bINIIdentical,
		                      &nINIFirstDifferingLine))
		              {
			             return HTTPInternalServerError;
		              }

		              char FirstDiffText[96];
		              int nFirstDiffWritten = snprintf (
			          FirstDiffText, sizeof FirstDiffText,
			          "%s",
			          bINIIdentical ? "none" : "");

		              if (nFirstDiffWritten < 0 || (unsigned) nFirstDiffWritten >= sizeof FirstDiffText)
		              {
			             return HTTPInternalServerError;
		              }

		              if (!bINIIdentical)
		              {
			             nFirstDiffWritten = snprintf (
				         FirstDiffText, sizeof FirstDiffText,
				         "<span style=\"color:#C00000; font-weight:bold;\">%u</span>",
				         nINIFirstDifferingLine);

			             if (nFirstDiffWritten < 0 || (unsigned) nFirstDiffWritten >= sizeof FirstDiffText)
			             {
				            return HTTPInternalServerError;
			             }
		              }

		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 INI diff preview</title></head>"
			          "<body>"
			          "<h1>MiniJV880 INI diff preview</h1>"
			          "<p>This page is read-only. No file is modified here.</p>"
			          "<ul>"
			          "<li>Current active INI present: %s (%s)</li>"
			          "<li>Current staged INI present: %s (%s)</li>"
			          "<li>Identical now: %s</li>"
			          "<li>First differing line: %s</li>"
			          "</ul>"
			          "%s"
			          "<p><a href=\"/ini-apply\">Open INI apply precheck page</a></p>"
			          "<p><a href=\"/ini-validate\">Open staged INI validator page</a></p>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          BoolText (bINIExists),
			          KernelStatusInnerHTML (INISizeText),
			          BoolText (bINIStageExists),
			          KernelStatusInnerHTML (INIStageSizeText),
			          BoolText (bINIIdentical),
			          FirstDiffText,
			          bINIIdentical
			              ? "<p style=\"color:#0047CC; font-weight:bold;\">The active INI file and the staged INI file are currently identical.</p>"
			              : "<p style=\"color:#C00000; font-weight:bold;\">The active INI file and the staged INI file are currently different.</p>");

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
                        }                      
                      
                         else if (strcmp (pPath, "/ini-apply") == 0)
                        {
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIBackupExists = false;
	                   bool bINIIdentical = false;
	                   unsigned nINIFirstDifferingLine = 0;
	                   char INISizeText[64];
	                   char INIStageSizeText[64];
	                   char INIBackupSizeText[64];
	                   unsigned nMalformedLines = 0;
	                   unsigned nFirstMalformedLine = 0;
	                   char INIApplyReviewWarning[384];
	                   char INIDiffLinkText[96];

	                   if (!GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIBackupPath,
	                           &bINIBackupExists,
	                           INIBackupSizeText,
	                           sizeof INIBackupSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bApplyPossible = bINIExists && bINIStageExists;
	                   INIApplyReviewWarning[0] = '\0';
	                   INIDiffLinkText[0] = '\0';

	                   if (bINIStageExists)
	                   {
		              FILE *pInput = fopen (kINIStagePath, "rb");
		              if (pInput == 0)
		              {
			             return HTTPInternalServerError;
		              }

		              char Line[512];
		              unsigned nLineNumber = 0;

		              while (fgets (Line, sizeof Line, pInput) != 0)
		              {
			             nLineNumber++;

			             bool bCompleteLine =
			                    strchr (Line, '\n') != 0
			                 || feof (pInput);

			             if (!bCompleteLine)
			             {
				            int ch;
				            while ((ch = fgetc (pInput)) != EOF && ch != '\n')
				            {
				            }

				            nMalformedLines++;
				            if (nFirstMalformedLine == 0)
				            {
					       nFirstMalformedLine = nLineNumber;
				            }
				            continue;
			             }

			             size_t nLen = strlen (Line);
			             while (nLen > 0
			                 && (Line[nLen-1] == '\n' || Line[nLen-1] == '\r'))
			             {
				            Line[--nLen] = '\0';
			             }

			             char *pScan = Line;
			             while (*pScan == ' ' || *pScan == '\t')
			             {
				            pScan++;
			             }

			             if (*pScan == '\0' || *pScan == ';' || *pScan == '#')
			             {
				            continue;
			             }

			             char *pEquals = strchr (pScan, '=');
			             if (pEquals == 0 || pEquals == pScan || pEquals[1] == '\0')
			             {
				            nMalformedLines++;
				            if (nFirstMalformedLine == 0)
				            {
					       nFirstMalformedLine = nLineNumber;
				            }
			             }
		              }

		              if (ferror (pInput))
		              {
			             fclose (pInput);
			             return HTTPInternalServerError;
		              }

		              if (fclose (pInput) != 0)
		              {
			             return HTTPInternalServerError;
		              }

	                   }

	                   if (bINIExists && bINIStageExists)
	                   {
		              if (!GetFirstDifferingLine (
		                      kINIActivePath,
		                      kINIStagePath,
		                      &bINIIdentical,
		                      &nINIFirstDifferingLine))
		              {
			             return HTTPInternalServerError;
		              }

		              if (!bINIIdentical)
		              {
			             int nDiffLinkWritten = snprintf (
				         INIDiffLinkText, sizeof INIDiffLinkText,
				         "<p><a href=\"/ini-diff\">Open INI diff preview page</a></p>");

			             if (nDiffLinkWritten < 0 || (unsigned) nDiffLinkWritten >= sizeof INIDiffLinkText)
			             {
				            return HTTPInternalServerError;
			             }
		              }
	                   }

	                   if (nMalformedLines != 0 && bINIExists && bINIStageExists && !bINIIdentical)
	                   {
		              int nReviewWritten = snprintf (
			          INIApplyReviewWarning, sizeof INIApplyReviewWarning,
			          "<p style=\"color:#C00000; font-weight:bold;\">The staged INI has malformed setting line(s) and also differs from the active INI. First malformed line: %u. First differing line: %u. Review the INI validator precheck page and the INI diff preview before apply.</p>",
			          nFirstMalformedLine,
			          nINIFirstDifferingLine);

		              if (nReviewWritten < 0 || (unsigned) nReviewWritten >= sizeof INIApplyReviewWarning)
		              {
			             return HTTPInternalServerError;
		              }
	                   }
	                   else if (nMalformedLines != 0)
	                   {
		              int nReviewWritten = snprintf (
			          INIApplyReviewWarning, sizeof INIApplyReviewWarning,
			          "<p style=\"color:#C00000; font-weight:bold;\">The staged INI has malformed setting line(s). First malformed line: %u. Review the INI validator precheck page before apply.</p>",
			          nFirstMalformedLine);

		              if (nReviewWritten < 0 || (unsigned) nReviewWritten >= sizeof INIApplyReviewWarning)
		              {
			             return HTTPInternalServerError;
		              }
	                   }
	                   else if (bINIExists && bINIStageExists && !bINIIdentical)
	                   {
		              int nReviewWritten = snprintf (
			          INIApplyReviewWarning, sizeof INIApplyReviewWarning,
			          "<p style=\"color:#C00000; font-weight:bold;\">The staged INI differs from the active INI. First differing line: %u. Review the INI diff preview before apply.</p>",
			          nINIFirstDifferingLine);

		              if (nReviewWritten < 0 || (unsigned) nReviewWritten >= sizeof INIApplyReviewWarning)
		              {
			             return HTTPInternalServerError;
		              }
	                   }

	                   char INIApplyActionHTML[256];
	                   int nActionWritten = 0;

	                   if (bApplyPossible)
	                   {
		              nActionWritten = snprintf (
			          INIApplyActionHTML,
			          sizeof INIApplyActionHTML,
			          "<p><a href=\"/ini-apply-exec\" %s>Execute INI apply now</a></p>",
			          ActionLinkStyle ());
	                   }
	                   else
	                   {
		              nActionWritten = snprintf (
			          INIApplyActionHTML,
			          sizeof INIApplyActionHTML,
			          "<p style=\"color:#C00000; font-weight:bold;\">Apply cannot be executed now because one or more required files are missing.</p>");
	                   }

	                   if (nActionWritten < 0 || (unsigned)nActionWritten >= sizeof INIApplyActionHTML)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               StatusPage, sizeof StatusPage,
		               "<html>"
		               "<head><title>MiniJV880 INI apply precheck</title></head>"
		               "<body>"
		               "<h1>MiniJV880 INI apply precheck</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<ul>"
		               "<li>Current active INI present: %s (%s)</li>"
		               "<li>Current staged INI present: %s (%s)</li>"
		               "<li>Current backup INI present: %s (%s)</li>"
		               "<li>Existing backup would need explicit replacement during apply: %s</li>"
		               "<li>Apply possible now: %s</li>"
		               "<li>Automatic reboot performed by apply: %s</li>"
		               "<li>Explicit reboot available now: %s</li>"
		               "</ul>"
		               "%s"
		               "%s"
		               "%s"
		               "<p>Planned apply step:</p>"
		               "<ol>"
		               "<li>Handle or remove minijv880.ini.bak</li>"
		               "<li>Rename minijv880.ini to minijv880.ini.bak</li>"
		               "<li>Rename minijv880.ini.new to minijv880.ini</li>"
		               "<li>Do not reboot automatically</li>"
		               "</ol>"
		               "%s"
		               "%s"
		               "<p><a href=\"/ini-validate\">Open INI validator precheck page</a></p>"
		               "<p><a href=\"/kernel-reboot\">Open kernel reboot page</a></p>"
		               "<p><a href=\"/ini-upload\">Back to INI staging upload page</a></p>"
		               "<p><a href=\"/ini-status\">Back to INI status</a></p>"
		               "<p><a href=\"/ini\">Back to INI home</a></p>"
                               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               BoolText (bINIExists),
		               KernelStatusInnerHTML (INISizeText),
		               BoolText (bINIStageExists),
		               KernelStatusInnerHTML (INIStageSizeText),
		               BoolText (bINIBackupExists),
		               KernelStatusInnerHTML (INIBackupSizeText),
		               BoolText (bINIBackupExists),
		               BoolText (bApplyPossible),
		               BoolText (false),
		               BoolText (true),
		               !bINIExists
		                   ? "<p style=\"color:#C00000; font-weight:bold;\">The current active INI file is missing. Apply cannot proceed safely.</p>"
		                   : "",
		               !bINIStageExists
		                   ? "<p style=\"color:#C00000; font-weight:bold;\">No staged INI file is present. Apply cannot proceed.</p>"
		                   : "",
		               INIApplyReviewWarning,
		               INIApplyActionHTML,
		               INIDiffLinkText);

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = StatusPage;
                        }
                        else if (strcmp (pPath, "/ini-apply-exec") == 0)
                        {
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIBackupExists = false;
	                   char INISizeText[64];
	                   char INIStageSizeText[64];
	                   char INIBackupSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIBackupPath,
	                           &bINIBackupExists,
	                           INIBackupSizeText,
	                           sizeof INIBackupSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (!bINIExists || !bINIStageExists)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 INI apply not executed</title></head>"
			          "<body>"
			          "<h1>MiniJV880 INI apply not executed</h1>"
			          "<p>Apply was not executed because the required files are not present.</p>"
			          "<ul>"
			          "<li>Current active INI present: %s (%s)</li>"
			          "<li>Current staged INI present: %s (%s)</li>"
			          "<li>Current backup INI present: %s (%s)</li>"
			          "</ul>"
			          "<p><a href=\"/ini-apply\">Back to INI apply precheck</a></p>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          BoolText (bINIExists),
			          KernelStatusInnerHTML (INISizeText),
			          BoolText (bINIStageExists),
			          KernelStatusInnerHTML (INIStageSizeText),
			          BoolText (bINIBackupExists),
			          KernelStatusInnerHTML (INIBackupSizeText));

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              bool bOldBackupRemoved = false;
		              bool bRenamedActiveToBackup = false;
		              bool bRenamedStageToActive = false;
		              bool bRollbackOK = false;

		              if (bINIBackupExists)
		              {
			 if (remove (kINIBackupPath) != 0)
			 {
			     int nWritten = snprintf (
			         StatusPage, sizeof StatusPage,
			         "<html>"
			         "<head><title>MiniJV880 INI apply not executed</title></head>"
			         "<body>"
			         "<h1 style=\"color:#C00000;\">MiniJV880 INI apply not executed</h1>"
			         "<p style=\"color:#C00000; font-weight:bold;\">The existing backup INI file could not be removed, so apply was not started.</p>"
			         "<ul>"
			         "<li>Current active INI present: %s (%s)</li>"
			         "<li>Current staged INI present: %s (%s)</li>"
			         "<li>Current backup INI present: %s (%s)</li>"
			         "</ul>"
			         "<p><a href=\"/ini-apply\">Back to INI apply precheck</a></p>"
			         "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			         "<p><a href=\"/ini\">Back to INI home</a></p>"
                                 "<p><a href=\"/\">Back to home</a></p>"
			         "</body>"
			         "</html>",
			         BoolText (bINIExists),
			         KernelStatusInnerHTML (INISizeText),
			         BoolText (bINIStageExists),
			         KernelStatusInnerHTML (INIStageSizeText),
			         BoolText (bINIBackupExists),
			         KernelStatusInnerHTML (INIBackupSizeText));

			     if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			     {
			         return HTTPInternalServerError;
			     }

			     pBody = StatusPage;
			 }
			 else
			 {
			     bOldBackupRemoved = true;
			 }
		              }

		              if (pBody == 0)
		              {
			 if (rename (kINIActivePath, kINIBackupPath) != 0)
			 {
			     int nWritten = snprintf (
			         StatusPage, sizeof StatusPage,
			         "<html>"
			         "<head><title>MiniJV880 INI apply not executed</title></head>"
			         "<body>"
			         "<h1 style=\"color:#C00000;\">MiniJV880 INI apply not executed</h1>"
			         "<p style=\"color:#C00000; font-weight:bold;\">The current active INI file could not be moved to the backup name. No apply was completed.</p>"
			         "<ul>"
			         "<li>Previous backup removed: %s</li>"
			         "<li>Current active INI renamed to backup: no</li>"
			         "<li>Staged INI promoted to active: no</li>"
			         "<li>Automatic reboot performed: no</li>"
			         "</ul>"
			         "<p><a href=\"/ini-apply\">Back to INI apply precheck</a></p>"
			         "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			         "<p><a href=\"/ini\">Back to INI home</a></p>"
                                 "<p><a href=\"/\">Back to home</a></p>"
			         "</body>"
			         "</html>",
			         BoolText (bOldBackupRemoved));

			     if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			     {
			         return HTTPInternalServerError;
			     }

			     pBody = StatusPage;
			 }
			 else
			 {
			     bRenamedActiveToBackup = true;

			     if (rename (kINIStagePath, kINIActivePath) != 0)
			     {
			         if (rename (kINIBackupPath, kINIActivePath) == 0)
			         {
				     bRollbackOK = true;
			         }

			         int nWritten = snprintf (
			             StatusPage, sizeof StatusPage,
			             "<html>"
			             "<head><title>MiniJV880 INI apply incomplete</title></head>"
			             "<body>"
			             "<h1 style=\"color:#C00000;\">MiniJV880 INI apply incomplete</h1>"
			             "<p style=\"color:#C00000; font-weight:bold;\">The staged INI file could not be promoted to active. A rollback was attempted immediately.</p>"
			             "<ul>"
			             "<li>Previous backup removed: %s</li>"
			             "<li>Current active INI renamed to backup: %s</li>"
			             "<li>Staged INI promoted to active: no</li>"
			             "<li>Rollback restored original active INI: %s</li>"
			             "<li>Automatic reboot performed: no</li>"
			             "</ul>"
			             "<p><a href=\"/ini-apply\">Back to INI apply precheck</a></p>"
			             "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			             "<p><a href=\"/ini\">Back to INI home</a></p>"
                                     "<p><a href=\"/\">Back to home</a></p>"
			             "</body>"
			             "</html>",
			             BoolText (bOldBackupRemoved),
			             BoolText (bRenamedActiveToBackup),
			             BoolText (bRollbackOK));

			         if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			         {
				     return HTTPInternalServerError;
			         }

			         pBody = StatusPage;
			     }
			     else
			     {
			         bRenamedStageToActive = true;

			         int nWritten = snprintf (
			             StatusPage, sizeof StatusPage,
			             "<html>"
			             "<head><title>MiniJV880 INI apply completed</title></head>"
			             "<body>"
			             "<h1>MiniJV880 INI apply completed</h1>"
			             "<p>The staged INI file has been promoted to the active INI file.</p>"
			             "<ul>"
			             "<li>Previous backup removed: %s</li>"
			             "<li>Current active INI renamed to backup: %s</li>"
			             "<li>Staged INI promoted to active: %s</li>"
			             "<li>Automatic reboot performed: no</li>"
			             "</ul>"
			             "<p><a href=\"/kernel-reboot\">Open kernel reboot page</a></p>"
			             "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			             "<p><a href=\"/ini\">Back to INI home</a></p>"
                                     "<p><a href=\"/\">Back to home</a></p>"
			             "</body>"
			             "</html>",
			             BoolText (bOldBackupRemoved),
			             BoolText (bRenamedActiveToBackup),
			             BoolText (bRenamedStageToActive));

			         if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			         {
				     return HTTPInternalServerError;
			         }

			         pBody = StatusPage;
			     }
			 }
		              }
	                   }
                        }
    
                        else if (strcmp (pPath, "/ini-backup-delete") == 0)
                        {
	                   bool bINIExists = false;
	                   bool bINIStageExists = false;
	                   bool bINIBackupExists = false;
	                   char INISizeText[64];
	                   char INIStageSizeText[64];
	                   char INIBackupSizeText[64];
	                   char INIBackupDigestText[32];

	                   if (!GetKernelFileStatusText (
	                           kINIActivePath,
	                           &bINIExists,
	                           INISizeText,
	                           sizeof INISizeText)
	                       || !GetKernelFileStatusText (
	                           kINIStagePath,
	                           &bINIStageExists,
	                           INIStageSizeText,
	                           sizeof INIStageSizeText)
	                       || !GetKernelFileStatusText (
	                           kINIBackupPath,
	                           &bINIBackupExists,
	                           INIBackupSizeText,
	                           sizeof INIBackupSizeText)
	                       || !GetKernelFileDigestText (
	                           kINIBackupPath,
	                           INIBackupDigestText,
	                           sizeof INIBackupDigestText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (!bINIBackupExists)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 INI backup delete not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 INI backup delete not executed</h1>"
			          "<p style=\"color:#C00000; font-weight:bold;\">No INI backup file is currently present, so nothing was removed.</p>"
			          "<ul>"
			          "<li>Current active INI present: %s (%s)</li>"
			          "<li>Current staged INI present: %s (%s)</li>"
			          "<li>Current backup INI present: %s (%s)</li>"
			          "<li>Automatic reboot performed: no</li>"
			          "</ul>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          BoolText (bINIExists),
			          KernelStatusInnerHTML (INISizeText),
			          BoolText (bINIStageExists),
			          KernelStatusInnerHTML (INIStageSizeText),
			          BoolText (bINIBackupExists),
			          KernelStatusInnerHTML (INIBackupSizeText));

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else if (remove (kINIBackupPath) != 0)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 INI backup delete not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 INI backup delete not executed</h1>"
			          "<p style=\"color:#C00000; font-weight:bold;\">The INI backup file could not be removed.</p>"
			          "<ul>"
			          "<li>INI backup file size: %s</li>"
			          "<li>INI backup file digest: %s</li>"
			          "<li>Current active INI modified: no</li>"
			          "<li>Current staged INI modified: no</li>"
			          "</ul>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          KernelStatusHTML (INIBackupSizeText),
			          KernelStatusHTML (INIBackupDigestText));

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 INI backup deleted</title></head>"
			          "<body>"
			          "<h1>MiniJV880 INI backup deleted</h1>"
			          "<p>The INI backup file has been removed.</p>"
			          "<ul>"
			          "<li>Removed INI backup size: %s</li>"
			          "<li>Removed INI backup digest: %s</li>"
			          "<li>Current active INI modified: no</li>"
			          "<li>Current staged INI modified: no</li>"
			          "</ul>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          KernelStatusHTML (INIBackupSizeText),
			          KernelStatusHTML (INIBackupDigestText));

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
                        }    
    
                        else if (strcmp (pPath, "/ini-download") == 0)
                        {
	                   FILE *pInput = fopen (kINIActivePath, "rb");
	                   if (pInput == 0)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>INI download not available</title></head>"
			          "<body>"
			          "<h1>INI download not available</h1>"
			          "<p>The active INI file is currently missing, so nothing can be downloaded.</p>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>");

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              if (fseek (pInput, 0, SEEK_END) != 0)
		              {
			             fclose (pInput);
			             return HTTPInternalServerError;
		              }

		              long nFileSize = ftell (pInput);
		              if (nFileSize < 0)
		              {
			             fclose (pInput);
			             return HTTPInternalServerError;
		              }

		              if ((unsigned long) nFileSize > *pLength)
		              {
			             fclose (pInput);

			             int nWritten = snprintf (
				         StatusPage, sizeof StatusPage,
				         "<html>"
				         "<head><title>INI download unavailable</title></head>"
				         "<body>"
				         "<h1>INI download unavailable</h1>"
				         "<p>The active INI file is larger than the current HTTP response buffer, so it cannot be downloaded through this route right now.</p>"
				         "<ul>"
				         "<li>INI file size: %ld bytes</li>"
				         "<li>HTTP buffer limit: %u bytes</li>"
				         "</ul>"
				         "<p><a href=\"/ini-status\">Back to INI status</a></p>"
				         "<p><a href=\"/ini\">Back to INI home</a></p>"
                                         "<p><a href=\"/\">Back to home</a></p>"
				         "</body>"
				         "</html>",
				         nFileSize,
				         *pLength);

			             if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			             {
				            return HTTPInternalServerError;
			             }

			             pBody = StatusPage;
		              }
		              else
		              {
			             if (fseek (pInput, 0, SEEK_SET) != 0)
			             {
				            fclose (pInput);
				            return HTTPInternalServerError;
			             }

			             if (nFileSize > 0)
			             {
				            size_t nRead = fread (pBuffer, 1, (size_t) nFileSize, pInput);
				            if (nRead != (size_t) nFileSize)
				            {
					       fclose (pInput);
					       return HTTPInternalServerError;
				            }
			             }

			             fclose (pInput);

			             *pLength = (unsigned) nFileSize;
			             *ppContentType = "application/octet-stream";
			             return HTTPOK;
		              }
	                   }
                        }
    
                        else if (strcmp (pPath, "/ini-backup-download") == 0)
                        {
	                   FILE *pInput = fopen (kINIBackupPath, "rb");
	                   if (pInput == 0)
	                   {
		              int nWritten = snprintf (
			          StatusPage, sizeof StatusPage,
			          "<html>"
			          "<head><title>MiniJV880 INI backup download not available</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 INI backup download not available</h1>"
			          "<p style=\"color:#C00000; font-weight:bold;\">No INI backup file is currently present, so nothing can be downloaded.</p>"
			          "<p><a href=\"/ini-status\">Back to INI status</a></p>"
			          "<p><a href=\"/ini\">Back to INI home</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>");

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
		              {
			             return HTTPInternalServerError;
		              }

		              pBody = StatusPage;
	                   }
	                   else
	                   {
		              if (fseek (pInput, 0, SEEK_END) != 0)
		              {
			             fclose (pInput);
			             return HTTPInternalServerError;
		              }

		              long nFileSize = ftell (pInput);
		              if (nFileSize < 0)
		              {
			             fclose (pInput);
			             return HTTPInternalServerError;
		              }

		              if ((unsigned long) nFileSize > *pLength)
		              {
			             fclose (pInput);

			             int nWritten = snprintf (
				         StatusPage, sizeof StatusPage,
				         "<html>"
				         "<head><title>MiniJV880 INI backup download unavailable</title></head>"
				         "<body>"
				         "<h1 style=\"color:#C00000;\">MiniJV880 INI backup download unavailable</h1>"
				         "<p style=\"color:#C00000; font-weight:bold;\">The backup INI file is larger than the current HTTP response buffer, so it cannot be downloaded through this route right now.</p>"
				         "<ul>"
				         "<li>INI backup file size: %ld bytes</li>"
				         "<li>HTTP buffer limit: %u bytes</li>"
				         "</ul>"
				         "<p><a href=\"/ini-status\">Back to INI status</a></p>"
				         "<p><a href=\"/ini\">Back to INI home</a></p>"
                                         "<p><a href=\"/\">Back to home</a></p>"
				         "</body>"
				         "</html>",
				         nFileSize,
				         *pLength);

			             if (nWritten < 0 || (unsigned) nWritten >= sizeof StatusPage)
			             {
				            return HTTPInternalServerError;
			             }

			             pBody = StatusPage;
		              }
		              else
		              {
			             if (fseek (pInput, 0, SEEK_SET) != 0)
			             {
				            fclose (pInput);
				            return HTTPInternalServerError;
			             }

			             if (nFileSize > 0)
			             {
				            size_t nRead = fread (pBuffer, 1, (size_t) nFileSize, pInput);
				            if (nRead != (size_t) nFileSize)
				            {
					       fclose (pInput);
					       return HTTPInternalServerError;
				            }
			             }

			             fclose (pInput);

			             *pLength = (unsigned) nFileSize;
			             *ppContentType = "application/octet-stream";
			             return HTTPOK;
		              }
	                   }
                        }                      
                      
                      else if (strcmp (pPath, "/kernel-status") == 0)
                      {
	                   bool bKernelExists = false;
	                   bool bStageExists = false;
	                   bool bBackupExists = false;
	                   char KernelSizeText[64];
	                   char StageSizeText[64];
	                   char BackupSizeText[64];
	                   char KernelDigestText[32];
	                   char StageDigestText[32];
	                   char BackupDigestText[32];

	                   if (!GetKernelFileStatusText (
	                           kKernelActivePath,
	                           &bKernelExists,
	                           KernelSizeText,
	                           sizeof KernelSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelStagePath,
	                           &bStageExists,
	                           StageSizeText,
	                           sizeof StageSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelBackupPath,
	                           &bBackupExists,
	                           BackupSizeText,
	                           sizeof BackupSizeText)
	                       || !GetKernelFileDigestText (
	                           kKernelActivePath,
	                           KernelDigestText,
	                           sizeof KernelDigestText)
	                       || !GetKernelFileDigestText (
	                           kKernelStagePath,
	                           StageDigestText,
	                           sizeof StageDigestText)
	                       || !GetKernelFileDigestText (
	                           kKernelBackupPath,
	                           BackupDigestText,
	                           sizeof BackupDigestText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               PNPage, sizeof PNPage,
		               "<html>"
		               "<head><title>MiniJV880 kernel status</title></head>"
		               "<body>"
		               "<h1>MiniJV880 kernel status</h1>"
		               "<p>This page is read-only. No file is modified here.</p>"
		               "<ul>"
		               "<li>SD:/kernel8-rpi4.img - exists: %s - size: %s - digest: %s</li>"
		               "<li>SD:/kernel8-rpi4.img.new - exists: %s - size: %s - digest: %s</li>"
		               "<li>SD:/kernel8-rpi4.img.bak - exists: %s - size: %s - digest: %s</li>"
		               "</ul>"
		               "<p>If two files have the same size, the digest helps determine whether their contents are actually different.</p>"
		               "<p><a href=\"/kernel-status.txt\">Open plain text kernel status endpoint</a></p>"
		               "<p><a href=\"/kernel-activate\">Open kernel activate precheck page</a></p>"
		               "<p><a href=\"/kernel-reboot\">Open kernel reboot page</a></p>"
		               "%s"
		               "<p><a href=\"/status\">Back to status</a></p>"
                               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               BoolText (bKernelExists),
		               KernelStatusHTML (KernelSizeText),
		               KernelStatusHTML (KernelDigestText),
		               BoolText (bStageExists),
		               KernelStatusHTML (StageSizeText),
		               KernelStatusHTML (StageDigestText),
		               BoolText (bBackupExists),
		               KernelStatusHTML (BackupSizeText),
		               KernelStatusHTML (BackupDigestText),
		               bBackupExists
		                   ? "<p><a href=\"/kernel-backup-delete-exec\">Delete current kernel backup now</a></p>"
		                   : "<p style=\"color:#C00000; font-weight:bold;\">No kernel backup file is currently present.</p>");

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = PNPage;
                      }
                      else if (strcmp (pPath, "/kernel-activate") == 0)
                      {
	                   TBootLayoutInfo BootLayout;
	                   DetectBootLayout (&BootLayout);

	                   const char *pKernelActivePath = BootLayout.ManagedKernelActivePath;
	                   const char *pKernelStagePath = BootLayout.ManagedKernelStagePath;
	                   const char *pKernelBackupPath = BootLayout.ManagedKernelBackupPath;

	                   bool bKernelExists = false;
	                   bool bStageExists = false;
	                   bool bBackupExists = false;
	                   char KernelSizeText[64];
	                   char StageSizeText[64];
	                   char BackupSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           pKernelActivePath,
	                           &bKernelExists,
	                           KernelSizeText,
	                           sizeof KernelSizeText)
	                       || !GetKernelFileStatusText (
	                           pKernelStagePath,
	                           &bStageExists,
	                           StageSizeText,
	                           sizeof StageSizeText)
	                       || !GetKernelFileStatusText (
	                           pKernelBackupPath,
	                           &bBackupExists,
	                           BackupSizeText,
	                           sizeof BackupSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   bool bCanActivate = bKernelExists && bStageExists;

	                   char KernelActivateActionHTML[256];
	                   if (bCanActivate)
	                   {
		              int nActionWritten = snprintf (
			          KernelActivateActionHTML,
			          sizeof KernelActivateActionHTML,
			          "<p><a href=\"/kernel-activate-exec\" %s>Execute kernel activate now</a></p>",
			          ActionLinkStyle ());

		              if (nActionWritten < 0 || (unsigned)nActionWritten >= sizeof KernelActivateActionHTML)
		              {
			          return HTTPInternalServerError;
		              }
	                   }
	                   else
	                   {
		              snprintf (
			          KernelActivateActionHTML,
			          sizeof KernelActivateActionHTML,
			          "<p style=\"color:#C00000; font-weight:bold;\">Activation cannot be executed now because one or more required files are missing.</p>");
	                   }

	                   int nWritten = snprintf (
		               PNPage, sizeof PNPage,
		               "<html>"
		               "<head><title>MiniJV880 kernel activate</title></head>"
		               "<body>"
		               "<h1>MiniJV880 kernel activate</h1>"
		               "<p>This page checks whether activation is possible and exposes the explicit activation command.</p>"
		               "<ul>"
		               "<li>Current active kernel present: %s (%s)</li>"
		               "<li>Current staged kernel present: %s (%s)</li>"
		               "<li>Current kernel backup present: %s (%s)</li>"
		               "<li>Activation possible now: %s</li>"
		               "</ul>"
		               "<p>Planned activate step:</p>"
		               "<ol>"
		               "<li>Handle or remove kernel8-rpi4.img.bak</li>"
		               "<li>Rename kernel8-rpi4.img to kernel8-rpi4.img.bak</li>"
		               "<li>Rename kernel8-rpi4.img.new to kernel8-rpi4.img</li>"
		               "<li>Do not reboot automatically</li>"
		               "</ol>"
		               "%s"
		               "%s"
		               "</body>"
		               "</html>",
		               BoolText (bKernelExists),
		               KernelStatusInnerHTML (KernelSizeText),
		               BoolText (bStageExists),
		               KernelStatusInnerHTML (StageSizeText),
		               BoolText (bBackupExists),
		               KernelStatusInnerHTML (BackupSizeText),
		               BoolText (bCanActivate),
		               KernelActivateActionHTML,
		               KernelCommonFooterHTML ());

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = PNPage;
                      }
                      else if (strcmp (pPath, "/kernel-activate-exec") == 0)
                      {
	                   TBootLayoutInfo BootLayout;
	                   DetectBootLayout (&BootLayout);

	                   const char *pKernelActivePath = BootLayout.ManagedKernelActivePath;
	                   const char *pKernelStagePath = BootLayout.ManagedKernelStagePath;
	                   const char *pKernelBackupPath = BootLayout.ManagedKernelBackupPath;

	                   bool bKernelExists = false;
	                   bool bStageExists = false;
	                   bool bBackupExists = false;
	                   char KernelSizeText[64];
	                   char StageSizeText[64];
	                   char BackupSizeText[64];

	                   if (!GetKernelFileStatusText (
	                           pKernelActivePath,
	                           &bKernelExists,
	                           KernelSizeText,
	                           sizeof KernelSizeText)
	                       || !GetKernelFileStatusText (
	                           pKernelStagePath,
	                           &bStageExists,
	                           StageSizeText,
	                           sizeof StageSizeText)
	                       || !GetKernelFileStatusText (
	                           pKernelBackupPath,
	                           &bBackupExists,
	                           BackupSizeText,
	                           sizeof BackupSizeText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (!bKernelExists || !bStageExists)
	                   {
		              int nWritten = snprintf (
			          PNPage, sizeof PNPage,
			          "<html>"
			          "<head><title>MiniJV880 kernel activate not executed</title></head>"
			          "<body>"
			          "<h1>MiniJV880 kernel activate not executed</h1>"
			          "<p>Activation was not executed because the required files are not present.</p>"
			          "<ul>"
			          "<li>Current active kernel present: %s (%s)</li>"
			          "<li>Current staged kernel present: %s (%s)</li>"
			          "<li>Current kernel backup present: %s (%s)</li>"
			          "</ul>"
			          "<p><a href=\"/kernel-activate\">Back to kernel activate</a></p>"
			          "<p><a href=\"/kernel-status\">Back to kernel status</a></p>"
			          "<p><a href=\"/status\">Back to status</a></p>"
                                  "<p><a href=\"/\">Back to home</a></p>"
			          "</body>"
			          "</html>",
			          BoolText (bKernelExists),
			          KernelStatusInnerHTML (KernelSizeText),
			          BoolText (bStageExists),
			          KernelStatusInnerHTML (StageSizeText),
			          BoolText (bBackupExists),
			          KernelStatusInnerHTML (BackupSizeText));

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
		              {
			 return HTTPInternalServerError;
		              }

		              pBody = PNPage;
	                   }
	                   else
	                   {
		              bool bOldBackupRemoved = false;
		              bool bRenamedActiveToBackup = false;
		              bool bRenamedStageToActive = false;
		              bool bRollbackOK = false;

		              if (bBackupExists)
		              {
			 if (remove (pKernelBackupPath) != 0)
			 {
			     int nWritten = snprintf (
			         PNPage, sizeof PNPage,
			         "<html>"
			         "<head><title>MiniJV880 kernel activate not executed</title></head>"
			         "<body>"
			         "<h1>MiniJV880 kernel activate not executed</h1>"
			         "<p>The existing backup file could not be removed, so activation was not started.</p>"
			         "<ul>"
			         "<li>Current active kernel present: %s (%s)</li>"
			         "<li>Current staged kernel present: %s (%s)</li>"
			         "<li>Current kernel backup present: %s (%s)</li>"
			         "</ul>"
			         "%s"
			         "</body>"
			         "</html>",
			         BoolText (bKernelExists),
			         KernelStatusInnerHTML (KernelSizeText),
			         BoolText (bStageExists),
			         KernelStatusInnerHTML (StageSizeText),
			         BoolText (bBackupExists),
			         KernelStatusInnerHTML (BackupSizeText),
			         KernelActivateFooterHTML ());

			     if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
			     {
			         return HTTPInternalServerError;
			     }

			     pBody = PNPage;
			 }
			 else
			 {
			     bOldBackupRemoved = true;
			 }
		              }

		              if (pBody == 0)
		              {
			 if (rename (pKernelActivePath, pKernelBackupPath) != 0)
			 {
			     int nWritten = snprintf (
			         PNPage, sizeof PNPage,
			         "<html>"
			         "<head><title>MiniJV880 kernel activate not executed</title></head>"
			         "<body>"
			         "<h1 style=\"color:#C00000;\">MiniJV880 kernel activate not executed</h1>"
			         "<p style=\"color:#C00000; font-weight:bold;\">The current active kernel could not be moved to the backup name. No activation was completed.</p>"
			         "<ul>"
			         "<li>Previous backup removed: %s</li>"
			         "<li>Current active renamed to backup: no</li>"
			         "<li>Staged kernel promoted to active: no</li>"
			         "<li>Automatic reboot performed: no</li>"
			         "</ul>"
			         "%s"
			         "</body>"
			         "</html>",
			         BoolText (bOldBackupRemoved),
			         KernelActivateFooterHTML ());

			     if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
			     {
			         return HTTPInternalServerError;
			     }

			     pBody = PNPage;
			 }
			 else
			 {
			     bRenamedActiveToBackup = true;

			     if (rename (pKernelStagePath, pKernelActivePath) != 0)
			     {
			         if (rename (pKernelBackupPath, pKernelActivePath) == 0)
			         {
				     bRollbackOK = true;
			         }

			         int nWritten = snprintf (
			             PNPage, sizeof PNPage,
			             "<html>"
			             "<head><title>MiniJV880 kernel activate incomplete</title></head>"
			             "<body>"
			             "<h1 style=\"color:#C00000;\">MiniJV880 kernel activate incomplete</h1>"
			             "<p style=\"color:#C00000; font-weight:bold;\">The staged kernel could not be promoted to active. A rollback was attempted immediately.</p>"
			             "<ul>"
			             "<li>Previous backup removed: %s</li>"
			             "<li>Current active renamed to backup: %s</li>"
			             "<li>Staged kernel promoted to active: no</li>"
			             "<li>Rollback restored original active kernel: %s</li>"
			             "<li>Automatic reboot performed: no</li>"
			             "</ul>"
			             "%s"
			             "</body>"
			             "</html>",
			             BoolText (bOldBackupRemoved),
			             BoolText (bRenamedActiveToBackup),
			             BoolText (bRollbackOK),
			             KernelStatusActivateFooterHTML ());

			         if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
			         {
				     return HTTPInternalServerError;
			         }

			         pBody = PNPage;
			     }
			     else
			     {
			         bRenamedStageToActive = true;

			         int nWritten = snprintf (
			             PNPage, sizeof PNPage,
			             "<html>"
			             "<head><title>MiniJV880 kernel activate completed</title></head>"
			             "<body>"
			             "<h1>MiniJV880 kernel activate completed</h1>"
			             "<p>The staged kernel has been promoted to the active kernel file.</p>"
			             "<ul>"
			             "<li>Previous backup removed: %s</li>"
			             "<li>Current active renamed to backup: %s</li>"
			             "<li>Staged kernel promoted to active: %s</li>"
			             "<li>Automatic reboot performed: no</li>"
			             "</ul>"
			             "%s"
			             "</body>"
			             "</html>",
			             BoolText (bOldBackupRemoved),
			             BoolText (bRenamedActiveToBackup),
			             BoolText (bRenamedStageToActive),
			             KernelOpenStatusActivateFooterHTML ());

			         if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
			         {
				     return HTTPInternalServerError;
			         }

			         pBody = PNPage;
			     }
			 }
		              }
	                   }
                      }
                      else if (strcmp (pPath, "/kernel-backup-delete-exec") == 0)
                      {
	                   TBootLayoutInfo BootLayout;
	                   DetectBootLayout (&BootLayout);

	                   const char *pKernelActivePath = BootLayout.ManagedKernelActivePath;
	                   const char *pKernelStagePath = BootLayout.ManagedKernelStagePath;
	                   const char *pKernelBackupPath = BootLayout.ManagedKernelBackupPath;

	                   bool bKernelExists = false;
	                   bool bStageExists = false;
	                   bool bBackupExists = false;
	                   char KernelSizeText[64];
	                   char StageSizeText[64];
	                   char BackupSizeText[64];
	                   char BackupDigestText[32];

	                   if (!GetKernelFileStatusText (
	                           pKernelActivePath,
	                           &bKernelExists,
	                           KernelSizeText,
	                           sizeof KernelSizeText)
	                       || !GetKernelFileStatusText (
	                           pKernelStagePath,
	                           &bStageExists,
	                           StageSizeText,
	                           sizeof StageSizeText)
	                       || !GetKernelFileStatusText (
	                           pKernelBackupPath,
	                           &bBackupExists,
	                           BackupSizeText,
	                           sizeof BackupSizeText)
	                       || !GetKernelFileDigestText (
	                           pKernelBackupPath,
	                           BackupDigestText,
	                           sizeof BackupDigestText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   if (!bBackupExists)
	                   {
		              int nWritten = snprintf (
			          PNPage, sizeof PNPage,
			          "<html>"
			          "<head><title>MiniJV880 kernel backup delete not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 kernel backup delete not executed</h1>"
			          "<p style=\"color:#C00000; font-weight:bold;\">No kernel backup file is currently present, so nothing was removed.</p>"
			          "<ul>"
			          "<li>Current active kernel present: %s (%s)</li>"
			          "<li>Current staged kernel present: %s (%s)</li>"
			          "<li>Current kernel backup present: %s (%s)</li>"
			          "<li>Automatic reboot performed: no</li>"
			          "</ul>"
			          "%s"
			          "</body>"
			          "</html>",
			          BoolText (bKernelExists),
			          KernelSizeText,
			          BoolText (bStageExists),
			          StageSizeText,
			          BoolText (bBackupExists),
			          BackupSizeText,
			          KernelCommonFooterHTML ());

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
		              {
			 return HTTPInternalServerError;
		              }

		              pBody = PNPage;
	                   }
	                   else if (remove (pKernelBackupPath) != 0)
	                   {
		              int nWritten = snprintf (
			          PNPage, sizeof PNPage,
			          "<html>"
			          "<head><title>MiniJV880 kernel backup delete not executed</title></head>"
			          "<body>"
			          "<h1 style=\"color:#C00000;\">MiniJV880 kernel backup delete not executed</h1>"
			          "<p style=\"color:#C00000; font-weight:bold;\">The kernel backup file could not be removed.</p>"
			          "<ul>"
			          "<li>Kernel backup file size: %s</li>"
			          "<li>Kernel backup file digest: %s</li>"
			          "<li>Current active kernel modified: no</li>"
			          "<li>Current staged kernel modified: no</li>"
			          "</ul>"
			          "%s"
			          "</body>"
			          "</html>",
			          KernelStatusHTML (BackupSizeText),
			          KernelStatusHTML (BackupDigestText),
			          KernelCommonFooterHTML ());

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
		              {
			 return HTTPInternalServerError;
		              }

		              pBody = PNPage;
	                   }
	                   else
	                   {
		              int nWritten = snprintf (
			          PNPage, sizeof PNPage,
			          "<html>"
			          "<head><title>MiniJV880 kernel backup deleted</title></head>"
			          "<body>"
			          "<h1>MiniJV880 kernel backup deleted</h1>"
			          "<p>The kernel backup file has been removed.</p>"
			          "<ul>"
			          "<li>Removed kernel backup size: %s</li>"
			          "<li>Removed kernel backup digest: %s</li>"
			          "<li>Current active kernel modified: no</li>"
			          "<li>Current staged kernel modified: no</li>"
			          "</ul>"
			          "%s"
			          "</body>"
			          "</html>",
			          KernelStatusHTML (BackupSizeText),
			          KernelStatusHTML (BackupDigestText),
			          KernelOpenStatusFooterHTML ());

		              if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
		              {
			 return HTTPInternalServerError;
		              }

		              pBody = PNPage;
	                   }
                      }
                      else if (strcmp (pPath, "/kernel-reboot") == 0)
                      {
	                   bool bKernelExists = false;
	                   bool bStageExists = false;
	                   bool bBackupExists = false;
	                   char KernelSizeText[64];
	                   char StageSizeText[64];
	                   char BackupSizeText[64];
	                   char KernelDigestText[32];

	                   if (!GetKernelFileStatusText (
	                           kKernelActivePath,
	                           &bKernelExists,
	                           KernelSizeText,
	                           sizeof KernelSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelStagePath,
	                           &bStageExists,
	                           StageSizeText,
	                           sizeof StageSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelBackupPath,
	                           &bBackupExists,
	                           BackupSizeText,
	                           sizeof BackupSizeText)
	                       || !GetKernelFileDigestText (
	                           kKernelActivePath,
	                           KernelDigestText,
	                           sizeof KernelDigestText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               PNPage, sizeof PNPage,
		               "<html>"
		               "<head><title>MiniJV880 kernel reboot</title></head>"
		               "<body>"
		               "<h1>MiniJV880 kernel reboot</h1>"
		               "<p>This page exposes an explicit reboot command. It does not modify any kernel file.</p>"
		               "<ul>"
		               "<li>Current active kernel present: %s (%s)</li>"
		               "<li>Current active kernel digest: %s</li>"
		               "<li>Current staged kernel present: %s (%s)</li>"
		               "<li>Current kernel backup present: %s (%s)</li>"
		               "</ul>"
		               "<p>The reboot command below should return this page first, then reboot the unit about 2 seconds later.</p>"
		               "<p><a href=\"/kernel-reboot-exec\" %s>Execute kernel reboot now</a></p>"
		               "%s"
		               "</body>"
		               "</html>",
		               BoolText (bKernelExists),
		               KernelStatusInnerHTML (KernelSizeText),
		               KernelStatusHTML (KernelDigestText),
		               BoolText (bStageExists),
		               KernelStatusInnerHTML (StageSizeText),
		               BoolText (bBackupExists),
		               KernelStatusInnerHTML (BackupSizeText),
		               ActionLinkStyle (),
		               KernelCommonFooterHTML ());

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = PNPage;
                      }

                      else if (strcmp (pPath, "/kernel-reboot-exec") == 0)
                      {
	                   bool bKernelExists = false;
	                   bool bStageExists = false;
	                   bool bBackupExists = false;
	                   char KernelSizeText[64];
	                   char StageSizeText[64];
	                   char BackupSizeText[64];
	                   char KernelDigestText[32];

	                   if (!GetKernelFileStatusText (
	                           kKernelActivePath,
	                           &bKernelExists,
	                           KernelSizeText,
	                           sizeof KernelSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelStagePath,
	                           &bStageExists,
	                           StageSizeText,
	                           sizeof StageSizeText)
	                       || !GetKernelFileStatusText (
	                           kKernelBackupPath,
	                           &bBackupExists,
	                           BackupSizeText,
	                           sizeof BackupSizeText)
	                       || !GetKernelFileDigestText (
	                           kKernelActivePath,
	                           KernelDigestText,
	                           sizeof KernelDigestText))
	                   {
		              return HTTPInternalServerError;
	                   }

	                   int nWritten = snprintf (
		               PNPage, sizeof PNPage,
		               "<html>"
		               "<head><title>MiniJV880 kernel reboot scheduled</title></head>"
		               "<body>"
		               "<h1>MiniJV880 kernel reboot scheduled</h1>"
		               "<p>The unit will reboot in about 2 seconds.</p>"
		               "<ul>"
		               "<li>Current active kernel present: %s (%s)</li>"
		               "<li>Current active kernel digest: %s</li>"
		               "<li>Current staged kernel present: %s (%s)</li>"
		               "<li>Current kernel backup present: %s (%s)</li>"
		               "<li>Kernel files modified: no</li>"
		               "<li>Automatic reboot performed by activate: no</li>"
		               "</ul>"
		               "%s"
		               "</body>"
		               "</html>",
		               BoolText (bKernelExists),
		               KernelStatusInnerHTML (KernelSizeText),
		               KernelStatusHTML (KernelDigestText),
		               BoolText (bStageExists),
		               KernelStatusInnerHTML (StageSizeText),
		               BoolText (bBackupExists),
		               KernelStatusInnerHTML (BackupSizeText),
		               KernelCommonFooterHTML ());

	                   if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
	                   {
		              return HTTPInternalServerError;
	                   }

	                   pBody = PNPage;

	                   {
		              MiniJV880_ShowKernelRebootMessage();
	                   }

	                   {
		              CBcmWatchdog Watchdog;
		              Watchdog.Start (2);
	                   }
                      }
                      else if (strcmp (pPath, "/browse") == 0 || strcmp (pPath, "/browse/") == 0)
                      {
	                   int nWritten = snprintf (
		               BrowsePage, sizeof BrowsePage,
		               "<html>"
		               "<head><title>Browse: root</title></head>"
                               "<body>"
                               "<h1>Browse: root</h1>"
		               "<p>Select one of the exposed browse roots below. .syx files inside PN-JV80 subfolders can be downloaded and managed from their detail page.</p>"
		               "<ul>"
		               "%s"
		               "%s"
		               "</ul>"
		               "<p>This page lists the currently exposed browse roots.</p>"
		               "<p><a href=\"/status\">Back to status</a></p>"
                               "<p><a href=\"/\">Back to home</a></p>"
		               "</body>"
		               "</html>",
		               m_Config.m_bExposePNJV80 ? "<li><a href=\"/browse/PN-JV80\">PN-JV80</a></li>" : "",
		               m_Config.m_bExposeRoms   ? "<li><a href=\"/browse/roms\">roms</a></li>" : "");

	                 if (nWritten < 0 || (unsigned) nWritten >= sizeof BrowsePage)
	                 {
		            return HTTPInternalServerError;
	                 }

	                 pBody = BrowsePage;
                    }
                    else if (strcmp (pPath, "/browse/PN-JV80") == 0)
                    {
	                if (!m_Config.m_bExposePNJV80)
	                {
		            return HTTPNotFound;
	                }

	                DIR *pDir = opendir ("SD:/PN-JV80");

	                size_t nUsed = 0;
	                int nWritten = snprintf (
		            PNPage, sizeof PNPage,
		            "<html>"
                            "<head><title>Browse: PN-JV80</title></head>"
                            "<body>"
                            "<h1>Browse: PN-JV80</h1>"
		            "<p>Directory listing. Click a file for details. .syx files inside PN-JV80 subfolders can be downloaded and managed from their detail page. The Roland-PN subfolder is a special read-only folder: over HTTP it allows download only.</p>"
		            "<ul>");

	                if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
	                {
		            return HTTPInternalServerError;
	                }

	                nUsed = (size_t) nWritten;

	                if (pDir == 0)
	                {
		            nWritten = snprintf (
			        PNPage + nUsed, sizeof PNPage - nUsed,
			        "<li>Cannot open directory: SD:/PN-JV80</li>");

		            if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
		            {
			        return HTTPInternalServerError;
		            }

		            nUsed += (size_t) nWritten;
	                    }
	                    
	                    else
                            {
                               char Names[64][256];
                               bool IsDir[64];
                               unsigned nNames = 0;
                               bool bTruncated = false;

                               for (;;)
                               {
                                  struct dirent *pEntry = readdir (pDir);
                                  if (pEntry == 0)
                                  {
                                      break;
                                  }

                                  if (strcmp (pEntry->d_name, ".") == 0 || strcmp (pEntry->d_name, "..") == 0)
                                  {
                                      continue;
                                  }

                                  if (nNames >= 64)
                                  {
                                      bTruncated = true;
                                      break;
                                  }

                                  int nCopyWritten = snprintf (
                                      Names[nNames], sizeof Names[nNames],
                                      "%s",
                                      pEntry->d_name);

                                  if (nCopyWritten < 0 || (unsigned) nCopyWritten >= sizeof Names[nNames])
                                  {
                                      closedir (pDir);
                                      return HTTPInternalServerError;
                                  }

                                  char FullPath[512];
                                  int nPathWritten = snprintf (
                                      FullPath, sizeof FullPath,
                                      "SD:/PN-JV80/%s", pEntry->d_name);

                                  if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                                  {
                                      closedir (pDir);
                                      return HTTPInternalServerError;
                                  }

                                  DIR *pTestDir = opendir (FullPath);
                                  IsDir[nNames] = pTestDir != 0;
                                  if (pTestDir != 0)
                                  {
                                     closedir (pTestDir);
                                  }

                                  nNames++;
                              }

                              closedir (pDir);

                              for (unsigned i = 0; i + 1 < nNames; i++)
                              {
                                  for (unsigned j = i + 1; j < nNames; j++)
                                  {
                                      if (strcmp (Names[j], Names[i]) < 0)
                                      {
                                          char TempName[256];
                                          memcpy (TempName, Names[i], sizeof TempName);
                                          memcpy (Names[i], Names[j], sizeof Names[i]);
                                          memcpy (Names[j], TempName, sizeof Names[j]);

                                          bool bTempIsDir = IsDir[i];
                                          IsDir[i] = IsDir[j];
                                          IsDir[j] = bTempIsDir;
                                       }
                                   }
                               }

                               for (unsigned i = 0; i < nNames; i++)
                               {
                                  char EncodedName[768];
                                  if (!URLEncodePathSegment (Names[i], EncodedName, sizeof EncodedName))
                                  {
                                      return HTTPInternalServerError;
                                  }

                                  if (IsDir[i])
                                  {
                                      nWritten = snprintf (
                                          PNPage + nUsed, sizeof PNPage - nUsed,
                                          "<li><a href=\"/browse/PN-JV80/%s\">%s/</a></li>",
                                          EncodedName,
                                          Names[i]);
                                  }
                                  else
                                  {
                                     nWritten = snprintf (
                                        PNPage + nUsed, sizeof PNPage - nUsed,
                                        "<li><a href=\"/browse/PN-JV80/%s\">%s</a></li>",
                                        EncodedName,
                                        Names[i]);
                                  }

                                  if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                                  {
                                      return HTTPInternalServerError;
                                  }

                                  nUsed += (size_t) nWritten;
                              }

                              if (nNames == 0)
                              {
                                  nWritten = snprintf (
                                      PNPage + nUsed, sizeof PNPage - nUsed,
                                      "<li>(empty)</li>");

                                  if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                                  {
                                      return HTTPInternalServerError;
                                  }

                                  nUsed += (size_t) nWritten;
                              }

                              if (bTruncated)
                              {
                                  nWritten = snprintf (
                                     PNPage + nUsed, sizeof PNPage - nUsed,
                                     "<li>(listing truncated after 64 entries)</li>");

                                 if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                                 {
                                    return HTTPInternalServerError;
                                 }

                                 nUsed += (size_t) nWritten;
                             }
                         }

	                nWritten = snprintf (
		            PNPage + nUsed, sizeof PNPage - nUsed,
		            "</ul>"
		            "<p><a href=\"/dir-create/PN-JV80\">Create new subfolder</a></p>"
		            "<p><a href=\"/browse\">Back to browse root</a></p>"
		            "<p><a href=\"/\">Back to home</a></p>"
		            "</body>"
		            "</html>");

	                if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
	                {
		            return HTTPInternalServerError;
	                }

	                pBody = PNPage;
                    }
                    
                    else if (strcmp (pPath, "/dir-create/PN-JV80") == 0)
                    {
                        THTTPStatus Status = HandleCreateFolderPage (
                            m_Config,
                            PNPage,
                            sizeof PNPage,
                            &pBody);

                        if (Status != HTTPOK)
                        {
                            return Status;
                        }
                    }

                    else if (strcmp (pPath, "/dir-create-exec/PN-JV80") == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        bool bHaveNameParam = false;
                        bool bRequestedNameInvalid = false;
                        char RequestedName[256];
                        RequestedName[0] = '\0';

                        if (pParams != 0 && pParams[0] != '\0')
                        {
                            const char *pParam = pParams;

                            while (*pParam != '\0')
                            {
                                const char *pNext = strchr (pParam, '&');
                                size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

                                if (nParamLen >= 5 && strncmp (pParam, "name=", 5) == 0)
                                {
                                    bHaveNameParam = true;

                                    char RawValue[256];
                                    size_t nValueLen = nParamLen - 5;
                                    if (nValueLen >= sizeof RawValue)
                                    {
                                        return HTTPInternalServerError;
                                    }

                                    for (size_t i = 0; i < nValueLen; i++)
                                    {
                                        char ch = pParam[5 + i];
                                        RawValue[i] = ch == '+' ? ' ' : ch;
                                    }

                                    RawValue[nValueLen] = '\0';

                                    if (!URLDecode (RawValue, RequestedName, sizeof RequestedName))
                                    {
                                        bRequestedNameInvalid = true;
                                        RequestedName[0] = '\0';
                                    }

                                    break;
                                }

                                if (pNext == 0)
                                {
                                    break;
                                }

                                pParam = pNext + 1;
                            }
                        }

                        if (!bHaveNameParam
                            || RequestedName[0] == '\0'
                            || bRequestedNameInvalid
                            || HasSlash (RequestedName)
                            || IsDotName (RequestedName)
                            || IsRolandPNFolder (RequestedName))
                        {
                            int nWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Create folder not executed</title></head>"
                                "<body>"
                                "<h1>Create folder not executed</h1>"
                                "<p>Invalid folder name. Use a valid new subfolder name for PN-JV80.</p>"
                                "<p><a href=\"/dir-create/PN-JV80\">Back to create folder page</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>");

                            if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            pBody = PNPage;
                        }
                        else
                        {
                            char TargetPath[768];
                            int nTargetWritten = snprintf (
                                TargetPath, sizeof TargetPath,
                                "SD:/PN-JV80/%s", RequestedName);

                            if (nTargetWritten < 0 || (unsigned) nTargetWritten >= sizeof TargetPath)
                            {
                                return HTTPInternalServerError;
                            }

                            bool bTargetExists = false;

                            DIR *pTargetDir = opendir (TargetPath);
                            if (pTargetDir != 0)
                            {
                                closedir (pTargetDir);
                                bTargetExists = true;
                            }

                            if (!bTargetExists)
                            {
                                FILE *pTargetInput = fopen (TargetPath, "rb");
                                if (pTargetInput != 0)
                                {
                                    fclose (pTargetInput);
                                    bTargetExists = true;
                                }
                            }

                            if (bTargetExists)
                            {
                                int nWritten = snprintf (
                                    PNPage, sizeof PNPage,
                                    "<html>"
                                    "<head><title>Create folder not executed</title></head>"
                                    "<body>"
                                    "<h1>Create folder not executed</h1>"
                                    "<p>A file or folder with the same name already exists in PN-JV80. No folder has been created.</p>"
                                    "<ul>"
                                    "<li>Requested folder name: %s</li>"
                                    "</ul>"
                                    "<p><a href=\"/dir-create/PN-JV80\">Back to create folder page</a></p>"
                                    "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                    "<p><a href=\"/browse\">Back to browse root</a></p>"
                                    "<p><a href=\"/\">Back to home</a></p>"
                                    "</body>"
                                    "</html>",
                                    RequestedName);

                                if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                                {
                                    return HTTPInternalServerError;
                                }

                                pBody = PNPage;
                            }
                            else
                            {
                                if (mkdir (TargetPath, 0777) != 0)
                                {
                                    return HTTPInternalServerError;
                                }

                                char EncodedFolder[768];
                                if (!URLEncodePathSegment (RequestedName, EncodedFolder, sizeof EncodedFolder))
                                {
                                    return HTTPInternalServerError;
                                }

                                int nWritten = snprintf (
                                    PNPage, sizeof PNPage,
                                    "<html>"
                                    "<head><title>Folder created: PN-JV80/%s</title></head>"
                                    "<body>"
                                    "<h1>Folder created: PN-JV80/%s</h1>"
                                    "<p>The new subfolder has been created.</p>"
                                    "<ul>"
                                    "<li>Folder name: %s</li>"
                                    "<li>SD path: SD:/PN-JV80/%s</li>"
                                    "</ul>"
                                    "<p><a href=\"/browse/PN-JV80/%s\">Open folder</a></p>"
                                    "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                    "<p><a href=\"/browse\">Back to browse root</a></p>"
                                    "<p><a href=\"/\">Back to home</a></p>"
                                    "</body>"
                                    "</html>",
                                    RequestedName,
                                    RequestedName,
                                    RequestedName,
                                    RequestedName,
                                    EncodedFolder);

                                if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                                {
                                    return HTTPInternalServerError;
                                }

                                pBody = PNPage;
                            }
                        }
                    }

                    else if (strncmp (pPath, "/dir-rename/PN-JV80/", 20) == 0)
                    {
                        THTTPStatus Status = HandleRenameFolderPage (
                            m_Config,
                            pPath,
                            pParams,
                            PNPage,
                            sizeof PNPage,
                            &pBody);

                        if (Status != HTTPOK)
                        {
                            return Status;
                        }
                    }

                    else if (strncmp (pPath, "/dir-rename-exec/PN-JV80/", 25) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 25;
                        if (pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char FolderName[256];
                        if (!URLDecode (pEncodedName, FolderName, sizeof FolderName))
                        {
                            return HTTPNotFound;
                        }

                        if (FolderName[0] == '\0'
                            || HasSlash (FolderName)
                            || IsDotName (FolderName)
                            || IsRolandPNFolder (FolderName))
                        {
                            return HTTPNotFound;
                        }

                        char SourcePath[768];
                        int nSourceWritten = snprintf (
                            SourcePath, sizeof SourcePath,
                            "SD:/PN-JV80/%s", FolderName);

                        if (nSourceWritten < 0 || (unsigned) nSourceWritten >= sizeof SourcePath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pSourceDir = opendir (SourcePath);
                        if (pSourceDir == 0)
                        {
                            return HTTPNotFound;
                        }
                        closedir (pSourceDir);

                        bool bHaveNewNameParam = false;
                        bool bRequestedNameInvalid = false;
                        char RequestedName[256];
                        RequestedName[0] = '\0';

                        if (pParams != 0 && pParams[0] != '\0')
                        {
                            const char *pParam = pParams;

                            while (*pParam != '\0')
                            {
                                const char *pNext = strchr (pParam, '&');
                                size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

                                if (nParamLen >= 8 && strncmp (pParam, "newname=", 8) == 0)
                                {
                                    bHaveNewNameParam = true;

                                    char RawValue[256];
                                    size_t nValueLen = nParamLen - 8;
                                    if (nValueLen >= sizeof RawValue)
                                    {
                                        return HTTPInternalServerError;
                                    }

                                    for (size_t i = 0; i < nValueLen; i++)
                                    {
                                        char ch = pParam[8 + i];
                                        RawValue[i] = ch == '+' ? ' ' : ch;
                                    }

                                    RawValue[nValueLen] = '\0';

                                    if (!URLDecode (RawValue, RequestedName, sizeof RequestedName))
                                    {
                                        bRequestedNameInvalid = true;
                                        RequestedName[0] = '\0';
                                    }

                                    break;
                                }

                                if (pNext == 0)
                                {
                                    break;
                                }

                                pParam = pNext + 1;
                            }
                        }

                        char EncodedFolder[768];
                        if (!URLEncodePathSegment (FolderName, EncodedFolder, sizeof EncodedFolder))
                        {
                            return HTTPInternalServerError;
                        }

                        if (!bHaveNewNameParam
                            || RequestedName[0] == '\0'
                            || bRequestedNameInvalid
                            || HasSlash (RequestedName)
                            || IsDotName (RequestedName)
                            || IsRolandPNFolder (RequestedName)
                            || strcmp (RequestedName, FolderName) == 0)
                        {
                            int nWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Rename folder not executed</title></head>"
                                "<body>"
                                "<h1>Rename folder not executed</h1>"
                                "<p>Invalid new folder name. Use a different valid subfolder name for PN-JV80.</p>"
                                "<ul>"
                                "<li>Current folder name: %s</li>"
                                "</ul>"
                                "<p><a href=\"/dir-rename/PN-JV80/%s\">Back to rename folder page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                FolderName,
                                EncodedFolder,
                                EncodedFolder);

                            if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            pBody = PNPage;
                        }
                        else
                        {
                            char TargetPath[768];
                            int nTargetWritten = snprintf (
                                TargetPath, sizeof TargetPath,
                                "SD:/PN-JV80/%s", RequestedName);

                            if (nTargetWritten < 0 || (unsigned) nTargetWritten >= sizeof TargetPath)
                            {
                                return HTTPInternalServerError;
                            }

                            bool bTargetExists = false;

                            DIR *pTargetDir = opendir (TargetPath);
                            if (pTargetDir != 0)
                            {
                                closedir (pTargetDir);
                                bTargetExists = true;
                            }

                            if (!bTargetExists)
                            {
                                FILE *pTargetInput = fopen (TargetPath, "rb");
                                if (pTargetInput != 0)
                                {
                                    fclose (pTargetInput);
                                    bTargetExists = true;
                                }
                            }

                            if (bTargetExists)
                            {
                                int nWritten = snprintf (
                                    PNPage, sizeof PNPage,
                                    "<html>"
                                    "<head><title>Rename folder not executed</title></head>"
                                    "<body>"
                                    "<h1>Rename folder not executed</h1>"
                                    "<p>A file or folder with the requested new name already exists in PN-JV80. No folder has been modified.</p>"
                                    "<ul>"
                                    "<li>Current folder name: %s</li>"
                                    "<li>Requested new folder name: %s</li>"
                                    "</ul>"
                                    "<p><a href=\"/dir-rename/PN-JV80/%s\">Back to rename folder page</a></p>"
                                    "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                                    "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                    "<p><a href=\"/browse\">Back to browse root</a></p>"
                                    "<p><a href=\"/\">Back to home</a></p>"
                                    "</body>"
                                    "</html>",
                                    FolderName,
                                    RequestedName,
                                    EncodedFolder,
                                    EncodedFolder);

                                if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                                {
                                    return HTTPInternalServerError;
                                }

                                pBody = PNPage;
                            }
                            else
                            {
                                if (rename (SourcePath, TargetPath) != 0)
                                {
                                    return HTTPInternalServerError;
                                }

                                char EncodedRequested[768];
                                if (!URLEncodePathSegment (RequestedName, EncodedRequested, sizeof EncodedRequested))
                                {
                                    return HTTPInternalServerError;
                                }

                                int nWritten = snprintf (
                                    PNPage, sizeof PNPage,
                                    "<html>"
                                    "<head><title>Folder renamed: PN-JV80/%s</title></head>"
                                    "<body>"
                                    "<h1>Folder renamed: PN-JV80/%s</h1>"
                                    "<p>The subfolder has been renamed.</p>"
                                    "<ul>"
                                    "<li>Previous folder name: %s</li>"
                                    "<li>New folder name: %s</li>"
                                    "<li>Previous SD path: %s</li>"
                                    "<li>New SD path: %s</li>"
                                    "</ul>"
                                    "<p><a href=\"/browse/PN-JV80/%s\">Open renamed folder</a></p>"
                                    "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                    "<p><a href=\"/browse\">Back to browse root</a></p>"
                                    "<p><a href=\"/\">Back to home</a></p>"
                                    "</body>"
                                    "</html>",
                                    RequestedName,
                                    RequestedName,
                                    FolderName,
                                    RequestedName,
                                    SourcePath,
                                    TargetPath,
                                    EncodedRequested);

                                if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                                {
                                    return HTTPInternalServerError;
                                }

                                pBody = PNPage;
                            }
                        }
                    }

                    else if (strncmp (pPath, "/dir-delete/PN-JV80/", 20) == 0)
                    {
                        THTTPStatus Status = HandleDeleteFolderPage (
                            m_Config,
                            pPath,
                            pParams,
                            PNPage,
                            sizeof PNPage,
                            &pBody);

                        if (Status != HTTPOK)
                        {
                            return Status;
                        }
                    }

                    else if (strncmp (pPath, "/dir-siblings/PN-JV80/", 22) == 0)
                    {
                        THTTPStatus Status = HandleSiblingFolderListPage (
                            m_Config,
                            pPath,
                            pParams,
                            PNPage,
                            sizeof PNPage,
                            &pBody);

                        if (Status != HTTPOK)
                        {
                            return Status;
                        }
                    }

                    else if (strncmp (pPath, "/upload/PN-JV80/", 16) == 0)
                    {
                        THTTPStatus Status = HandleUploadPage (
                            m_Config,
                            pPath,
                            pParams,
                            PNPage,
                            sizeof PNPage,
                            &pBody);

                        if (Status != HTTPOK)
                        {
                            return Status;
                        }
                    }

else if (strncmp (pPath, "/upload-begin/PN-JV80/", 22) == 0)
{
	THTTPStatus Status = HandleUploadChunkBegin (
		m_Config,
		pPath,
		PNPage,
		sizeof PNPage,
		&pBody);

	if (Status != HTTPOK)
	{
		return Status;
	}
}

else if (strncmp (pPath, "/upload-chunk/PN-JV80/", 22) == 0)
{
	THTTPStatus Status = HandleUploadChunkData (
		m_Config,
		pPath,
		PNPage,
		sizeof PNPage,
		&pBody);

	if (Status != HTTPOK)
	{
		return Status;
	}
}

else if (strncmp (pPath, "/upload-finish/PN-JV80/", 23) == 0)
{
	THTTPStatus Status = HandleUploadChunkFinish (
		m_Config,
		pPath,
		PNPage,
		sizeof PNPage,
		&pBody);

	if (Status != HTTPOK)
	{
		return Status;
	}
}

else if (strncmp (pPath, "/dir-delete-exec/PN-JV80/", 25) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 25;
                        if (pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char FolderName[256];
                        if (!URLDecode (pEncodedName, FolderName, sizeof FolderName))
                        {
                            return HTTPNotFound;
                        }

                        if (FolderName[0] == '\0'
                            || HasSlash (FolderName)
                            || IsDotName (FolderName)
                            || IsRolandPNFolder (FolderName))
                        {
                            return HTTPNotFound;
                        }

                        char SourcePath[768];
                        int nSourceWritten = snprintf (
                            SourcePath, sizeof SourcePath,
                            "SD:/PN-JV80/%s", FolderName);

                        if (nSourceWritten < 0 || (unsigned) nSourceWritten >= sizeof SourcePath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pSourceDir = opendir (SourcePath);
                        if (pSourceDir == 0)
                        {
                            return HTTPNotFound;
                        }

                        bool bIsEmpty = true;

                        for (;;)
                        {
                            struct dirent *pEntry = readdir (pSourceDir);
                            if (pEntry == 0)
                            {
                                break;
                            }

                            if (IsDotName (pEntry->d_name))
                            {
                                continue;
                            }

                            bIsEmpty = false;
                            break;
                        }

                        closedir (pSourceDir);

                        char EncodedFolder[768];
                        if (!URLEncodePathSegment (FolderName, EncodedFolder, sizeof EncodedFolder))
                        {
                            return HTTPInternalServerError;
                        }

                        if (!bIsEmpty)
                        {
                            int nWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Delete folder not executed</title></head>"
                                "<body>"
                                "<h1>Delete folder not executed</h1>"
                                "<p>The subfolder is not empty. Only empty subfolders can be deleted.</p>"
                                "<ul>"
                                "<li>Folder name: %s</li>"
                                "<li>SD path: %s</li>"
                                "</ul>"
                                "<p><a href=\"/dir-delete/PN-JV80/%s\">Back to delete folder page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                FolderName,
                                SourcePath,
                                EncodedFolder,
                                EncodedFolder);

                            if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            pBody = PNPage;
                        }
                        else
                        {
                            if (remove (SourcePath) != 0)
                            {
                                return HTTPInternalServerError;
                            }

                            int nWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Folder deleted: PN-JV80/%s</title></head>"
                                "<body>"
                                "<h1>Folder deleted: PN-JV80/%s</h1>"
                                "<p>The empty subfolder has been deleted.</p>"
                                "<ul>"
                                "<li>Folder name: %s</li>"
                                "<li>Previous SD path: %s</li>"
                                "</ul>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                FolderName,
                                FolderName,
                                FolderName,
                                SourcePath);

                            if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            pBody = PNPage;
                        }
                    }

                    else if (strncmp (pPath, "/browse/PN-JV80/", 16) == 0)
            
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 16;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount > 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash != 0)
                        {
                            *pSlash = '\0';

                            const char *pParentName = ItemPath;
                            const char *pChildName = pSlash + 1;

                            if (pParentName[0] == '\0' || pChildName[0] == '\0')
                            {
                                return HTTPNotFound;
                            }

                            if (IsDotName (pParentName) || IsDotName (pChildName))
                            {
                                return HTTPNotFound;
                            }
                        }
                        else if (IsDotName (ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);
                            
                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pDir = opendir (FullPath);
 
                        if (pDir != 0)
                        {
                           if (nSlashCount != 0)
                           {
                              closedir (pDir);
                              return HTTPNotFound;
                           }

                           size_t nUsed = 0;
                              int nWritten = snprintf (
                              PNPage, sizeof PNPage,
                              "<html>"
                              "<head><title>Browse: PN-JV80/%s</title></head>"
                              "<body>"
                              "<h1>Browse: PN-JV80/%s</h1>"
                              "<p>Directory listing. One level only. Click a file for details. .syx files in this subfolder can be downloaded and managed from their detail page.</p>"
                              "<ul>",
                              ItemName, ItemName);

                           if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                           {
                              closedir (pDir);
                              return HTTPInternalServerError;
                           }

                           nUsed = (size_t) nWritten;

                           char Names[64][256];
                           bool IsDir[64];
                           unsigned nNames = 0;
                           bool bTruncated = false;

                           for (;;)
                           {
                              struct dirent *pEntry = readdir (pDir);
                              if (pEntry == 0)
                              {
                                  break;
                              }

                              if (IsDotName (pEntry->d_name))
                              {
                                  continue;
                              }

                              char ChildPath[768];
                              int nChildWritten = snprintf (
                                  ChildPath, sizeof ChildPath,
                                  "%s/%s", FullPath, pEntry->d_name);

                              if (nChildWritten < 0 || (unsigned) nChildWritten >= sizeof ChildPath)
                              {
                                  closedir (pDir);
                                  return HTTPInternalServerError;
                              }

                              DIR *pTestDir = opendir (ChildPath);
                              bool bIsDir = pTestDir != 0;
                              if (pTestDir != 0)
                              {
                                  closedir (pTestDir);
                              }

                              if (bIsDir)
                              {
                                  continue;
                              }

                              const char *pExt = strrchr (pEntry->d_name, '.');
                              if (pExt == 0
                                  || !((pExt[1] == 's' || pExt[1] == 'S')
                                    && (pExt[2] == 'y' || pExt[2] == 'Y')
                                    && (pExt[3] == 'x' || pExt[3] == 'X')
                                    && pExt[4] == '\0'))
                              {
                                  continue;
                              }

                              if (nNames >= 64)
                              {
                                  bTruncated = true;
                                  break;
                              }

                              int nCopyWritten = snprintf (
                                  Names[nNames], sizeof Names[nNames],
                                  "%s",
                                  pEntry->d_name);

                              if (nCopyWritten < 0 || (unsigned) nCopyWritten >= sizeof Names[nNames])
                              {
                                  closedir (pDir);
                                  return HTTPInternalServerError;
                              }

                              IsDir[nNames] = false;
                              nNames++;
                           }

                           closedir (pDir);

                           for (unsigned i = 0; i + 1 < nNames; i++)
                           {
                              for (unsigned j = i + 1; j < nNames; j++)
                              {
                                  if (strcmp (Names[j], Names[i]) < 0)
                                  {
                                  char TempName[256];
                                  memcpy (TempName, Names[i], sizeof TempName);
                                  memcpy (Names[i], Names[j], sizeof Names[i]);
                                  memcpy (Names[j], TempName, sizeof Names[j]);

                                  bool bTempIsDir = IsDir[i];
                                  IsDir[i] = IsDir[j];
                                  IsDir[j] = bTempIsDir;
                              }
                           }
                        }

                        for (unsigned i = 0; i < nNames; i++)
                        {
                        if (IsDir[i])
                        {
                            nWritten = snprintf (
                                PNPage + nUsed, sizeof PNPage - nUsed,
                                "<li>%s/</li>",
                                Names[i]);
                        }

                        else
                        {
                            char EncodedParent[768];
                            char EncodedChild[768];

                            if (!URLEncodePathSegment (ItemName, EncodedParent, sizeof EncodedParent)
                                || !URLEncodePathSegment (Names[i], EncodedChild, sizeof EncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            nWritten = snprintf (
                                PNPage + nUsed, sizeof PNPage - nUsed,
                                "<li><a href=\"/browse/PN-JV80/%s/%s\">%s</a></li>",
                                EncodedParent,
                                EncodedChild,
                                Names[i]);
                        }

                        if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                        {
                            return HTTPInternalServerError;
                        }

                        nUsed += (size_t) nWritten;
                    }

                    if (nNames == 0)
                    {
                        nWritten = snprintf (
                        PNPage + nUsed, sizeof PNPage - nUsed,
                        "<li>(empty)</li>");

                        if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                        {
                            return HTTPInternalServerError;
                        }

                        nUsed += (size_t) nWritten;
                    }

                    if (bTruncated)
                    {
                        nWritten = snprintf (
                        PNPage + nUsed, sizeof PNPage - nUsed,
                        "<li>(listing truncated after 64 entries)</li>");

                    if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                    {
                        return HTTPInternalServerError;
                    }

                    nUsed += (size_t) nWritten;
                }

                nWritten = snprintf (
                    PNPage + nUsed, sizeof PNPage - nUsed,
                    "</ul>");

                if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                {
                    return HTTPInternalServerError;
                }

                nUsed += (size_t) nWritten;

                if (!IsRolandPNFolder (ItemName))
                {
                    char EncodedFolder[768];
                    if (!URLEncodePathSegment (ItemName, EncodedFolder, sizeof EncodedFolder))
                    {
                        return HTTPInternalServerError;
                    }

                    nWritten = snprintf (
                        PNPage + nUsed, sizeof PNPage - nUsed,
                        "<p><a href=\"/upload/PN-JV80/%s\">Upload a .syx file into this subfolder</a></p>"
                        "<p><a href=\"/dir-rename/PN-JV80/%s\">Rename this subfolder</a></p>"
                        "<p><a href=\"/dir-delete/PN-JV80/%s\">Delete this subfolder</a></p>",
                        EncodedFolder,
                        EncodedFolder,
                        EncodedFolder);

                    if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                    {
                        return HTTPInternalServerError;
                    }

                    nUsed += (size_t) nWritten;
                }

                nWritten = snprintf (
                    PNPage + nUsed, sizeof PNPage - nUsed,
                    "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                    "<p><a href=\"/browse\">Back to browse root</a></p>"
                    "<p><a href=\"/\">Back to home</a></p>"
                    "</body>"
                    "</html>");

                if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
                {
                    return HTTPInternalServerError;
                }

                pBody = PNPage;
            }
          
          else
          {
              FILE *pInput = fopen (FullPath, "rb");
              if (pInput == 0)
              {
                  return HTTPNotFound;
              }

              char SizeText[64];
              if (fseek (pInput, 0, SEEK_END) == 0)
              {
                  long nFileSize = ftell (pInput);
                  if (nFileSize >= 0)
                  {
                      snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                  }
                  else
                  {
                      snprintf (SizeText, sizeof SizeText, "unknown");
                  }
              }
              else
              {
                  snprintf (SizeText, sizeof SizeText, "unknown");
              }

              fclose (pInput);

              const char *pInfoText = "File detail page. No actions are available for this file.";

              char ActionSection[2048];
              ActionSection[0] = '\0';

              char ParentLinkSection[256];
              ParentLinkSection[0] = '\0';

              if (nSlashCount == 1)
              {
                  const char *pSlashInName = strchr (ItemName, '/');
                  if (pSlashInName != 0)
                  {
                      char ParentName[256];
                      char ChildName[256];

                      size_t nParentLen = (size_t) (pSlashInName - ItemName);
                      if (nParentLen == 0 || nParentLen >= sizeof ParentName)
                      {
                          return HTTPInternalServerError;
                      }

                      memcpy (ParentName, ItemName, nParentLen);
                      ParentName[nParentLen] = '\0';

                      int nChildWritten = snprintf (
                          ChildName, sizeof ChildName,
                          "%s", pSlashInName + 1);

                      if (nChildWritten < 0 || (unsigned) nChildWritten >= sizeof ChildName)
                      {
                          return HTTPInternalServerError;
                      }

                      const char *pExt = strrchr (ChildName, '.');
                      if (pExt != 0
                          && ((pExt[1] == 's' || pExt[1] == 'S')
                           && (pExt[2] == 'y' || pExt[2] == 'Y')
                           && (pExt[3] == 'x' || pExt[3] == 'X')
                           && pExt[4] == '\0'))
                      {
                          char EncodedParent[768];
                          char EncodedChild[768];

                          if (!URLEncodePathSegment (ParentName, EncodedParent, sizeof EncodedParent)
                              || !URLEncodePathSegment (ChildName, EncodedChild, sizeof EncodedChild))
                          {
                              return HTTPInternalServerError;
                          }

                          int nActionWritten;

                          if (IsRolandPNFolder (ParentName))
                          {
                              pInfoText = "File detail page for this protected .syx file. Only download is available over HTTP.";

                              nActionWritten = snprintf (
                                  ActionSection, sizeof ActionSection,
                                  "<h2>Actions</h2>"
                                  "<ul>"
                                  "<li><a href=\"/download/PN-JV80/%s/%s\">Download this .syx file</a></li>"
                                  "</ul>",
                                  EncodedParent,
                                  EncodedChild);
                          }
                          else
                          {
                              pInfoText = "File detail page for this .syx file. The actions below allow download, copy, rename, move and delete.";

                              nActionWritten = snprintf (
                                  ActionSection, sizeof ActionSection,
                                  "<h2>Actions</h2>"
                                  "<ul>"
                                  "<li><a href=\"/download/PN-JV80/%s/%s\">Download this .syx file</a></li>"
                                  "<li><a href=\"/copy/PN-JV80/%s/%s\">Copy this .syx file</a></li>"
                                  "<li><a href=\"/rename/PN-JV80/%s/%s\">Rename this .syx file</a></li>"
                                  "<li><a href=\"/move/PN-JV80/%s/%s\">Move this .syx file</a></li>"
                                  "<li><a href=\"/delete/PN-JV80/%s/%s\">Delete this .syx file</a></li>"
                                  "</ul>",
                                  EncodedParent,
                                  EncodedChild,
                                  EncodedParent,
                                  EncodedChild,
                                  EncodedParent,
                                  EncodedChild,
                                  EncodedParent,
                                  EncodedChild,
                                  EncodedParent,
                                  EncodedChild);
                          }

                          if (nActionWritten < 0 || (unsigned) nActionWritten >= sizeof ActionSection)
                          {
                              return HTTPInternalServerError;
                          }

                          int nParentLinkWritten = snprintf (
                              ParentLinkSection, sizeof ParentLinkSection,
                              "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>",
                              EncodedParent);

                          if (nParentLinkWritten < 0 || (unsigned) nParentLinkWritten >= sizeof ParentLinkSection)
                          {
                              return HTTPInternalServerError;
                          }
                      }
                  }
              }

              int nWritten = snprintf (
                  PNPage, sizeof PNPage,
                  "<html>"
                  "<head><title>File detail: PN-JV80/%s</title></head>"
                  "<body>"
                  "<h1>File detail: PN-JV80/%s</h1>"
                  "<p>%s</p>"
                  "%s"
                  "<ul>"
                  "<li>File name: %s</li>"
                  "<li>SD path: %s</li>"
                  "<li>File size: %s</li>"
                  "</ul>"
                  "%s"
                  "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                  "<p><a href=\"/browse\">Back to browse root</a></p>"
                  "<p><a href=\"/\">Back to home</a></p>"
                  "</body>"
                  "</html>",
                  ItemName,
                  ItemName,
                  pInfoText,
                  ActionSection,
                  ItemName,
                  FullPath,
                  SizeText,
                  ParentLinkSection);

              if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
              {
                  return HTTPInternalServerError;
              }

              pBody = PNPage;
          }
      }

                    else if (strncmp (pPath, "/copy/PN-JV80/", 14) == 0)
                    {
                        THTTPStatus Status = HandleCopyPage (
                            m_Config,
                            pPath,
                            pParams,
                            PNPage,
                            sizeof PNPage,
                            &pBody);

                        if (Status != HTTPOK)
                        {
                            return Status;
                        }
                    }

                    else if (strncmp (pPath, "/copy-exec/PN-JV80/", 19) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 19;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        char SizeText[64];
                        if (fseek (pInput, 0, SEEK_END) == 0)
                        {
                            long nFileSize = ftell (pInput);
                            if (nFileSize >= 0)
                            {
                                snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                            }
                            else
                            {
                                snprintf (SizeText, sizeof SizeText, "unknown");
                            }
                        }
                        else
                        {
                            snprintf (SizeText, sizeof SizeText, "unknown");
                        }

                        fclose (pInput);

                        bool bHaveDestParam = false;
                        bool bRequestedDestInvalid = false;
                        char RequestedDest[256];
                        RequestedDest[0] = '\0';

                        if (pParams != 0 && pParams[0] != '\0')
                        {
                            const char *pParam = pParams;

                            while (*pParam != '\0')
                            {
                                const char *pNext = strchr (pParam, '&');
                                size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

                                if (nParamLen >= 5 && strncmp (pParam, "dest=", 5) == 0)
                                {
                                    bHaveDestParam = true;

                                    char RawValue[256];
                                    size_t nValueLen = nParamLen - 5;
                                    if (nValueLen >= sizeof RawValue)
                                    {
                                        return HTTPInternalServerError;
                                    }

                                    for (size_t i = 0; i < nValueLen; i++)
                                    {
                                        char ch = pParam[5 + i];
                                        RawValue[i] = ch == '+' ? ' ' : ch;
                                    }

                                    RawValue[nValueLen] = '\0';

                                    if (!URLDecode (RawValue, RequestedDest, sizeof RequestedDest))
                                    {
                                        bRequestedDestInvalid = true;
                                        RequestedDest[0] = '\0';
                                    }

                                    break;
                                }

                                if (pNext == 0)
                                {
                                    break;
                                }

                                pParam = pNext + 1;
                            }
                        }

                        if (!bHaveDestParam
                            || RequestedDest[0] == '\0'
                            || bRequestedDestInvalid
                            || HasSlash (RequestedDest)
                            || IsDotName (RequestedDest)
                            || strcmp (RequestedDest, pParentName) == 0)
                        {
                            char InvalidEncodedParent[768];
                            char InvalidEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, InvalidEncodedParent, sizeof InvalidEncodedParent)
                                || !URLEncodePathSegment (pChildName, InvalidEncodedChild, sizeof InvalidEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nInvalidWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Copy not executed</title></head>"
                                "<body>"
                                "<h1>Copy not executed</h1>"
                                "<p>Invalid destination subfolder. Use an existing subfolder name different from the source folder.</p>"
                                "<ul>"
                                "<li>File name: %s</li>"
                                "<li>Source folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/copy/PN-JV80/%s/%s\">Back to copy page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                InvalidEncodedParent,
                                InvalidEncodedChild,
                                InvalidEncodedParent,
                                InvalidEncodedChild);

                            if (nInvalidWritten < 0 || (unsigned) nInvalidWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }

                        char TargetFolderPath[512];
                        int nTargetFolderWritten = snprintf (
                            TargetFolderPath, sizeof TargetFolderPath,
                            "SD:/PN-JV80/%s", RequestedDest);

                        if (nTargetFolderWritten < 0 || (unsigned) nTargetFolderWritten >= sizeof TargetFolderPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTargetDir = opendir (TargetFolderPath);
                        if (pTargetDir == 0)
                        {
                            char MissingEncodedParent[768];
                            char MissingEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, MissingEncodedParent, sizeof MissingEncodedParent)
                                || !URLEncodePathSegment (pChildName, MissingEncodedChild, sizeof MissingEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nMissingWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Copy not executed</title></head>"
                                "<body>"
                                "<h1>Copy not executed</h1>"
                                "<p>Destination subfolder does not exist. No file has been modified.</p>"
                                "<ul>"
                                "<li>File name: %s</li>"
                                "<li>Source folder: %s</li>"
                                "<li>Requested destination folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/copy/PN-JV80/%s/%s\">Back to copy page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                RequestedDest,
                                MissingEncodedParent,
                                MissingEncodedChild,
                                MissingEncodedParent,
                                MissingEncodedChild);

                            if (nMissingWritten < 0 || (unsigned) nMissingWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }
                        closedir (pTargetDir);

                        char TargetPath[768];
                        int nTargetWritten = snprintf (
                            TargetPath, sizeof TargetPath,
                            "SD:/PN-JV80/%s/%s", RequestedDest, pChildName);

                        if (nTargetWritten < 0 || (unsigned) nTargetWritten >= sizeof TargetPath)
                        {
                            return HTTPInternalServerError;
                        }

                        bool bTargetExists = false;

                        DIR *pTargetTestDir = opendir (TargetPath);
                        if (pTargetTestDir != 0)
                        {
                            closedir (pTargetTestDir);
                            bTargetExists = true;
                        }

                        if (!bTargetExists)
                        {
                            FILE *pTargetInput = fopen (TargetPath, "rb");
                            if (pTargetInput != 0)
                            {
                                fclose (pTargetInput);
                                bTargetExists = true;
                            }
                        }

                        if (bTargetExists)
                        {
                            char ConflictEncodedParent[768];
                            char ConflictEncodedChild[768];
                            char ConflictEncodedDest[768];

                            if (!URLEncodePathSegment (pParentName, ConflictEncodedParent, sizeof ConflictEncodedParent)
                                || !URLEncodePathSegment (pChildName, ConflictEncodedChild, sizeof ConflictEncodedChild)
                                || !URLEncodePathSegment (RequestedDest, ConflictEncodedDest, sizeof ConflictEncodedDest))
                            {
                                return HTTPInternalServerError;
                            }

                            int nConflictWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Copy not executed</title></head>"
                                "<body>"
                                "<h1>Copy not executed</h1>"
                                "<p>Target file already exists in destination folder. No file has been modified.</p>"
                                "<ul>"
                                "<li>File name: %s</li>"
                                "<li>Source folder: %s</li>"
                                "<li>Destination folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/copy/PN-JV80/%s/%s\">Back to copy page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s\">Back to destination folder</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                RequestedDest,
                                ConflictEncodedParent,
                                ConflictEncodedChild,
                                ConflictEncodedParent,
                                ConflictEncodedChild,
                                ConflictEncodedDest);

                            if (nConflictWritten < 0 || (unsigned) nConflictWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }
                            
                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
   
                        }

                        char EncodedDest[768];
                        char EncodedChild[768];
                        if (!URLEncodePathSegment (RequestedDest, EncodedDest, sizeof EncodedDest)
                            || !URLEncodePathSegment (pChildName, EncodedChild, sizeof EncodedChild))
                        {
                            return HTTPInternalServerError;
                        }

                        if (!CopyFileContents (FullPath, TargetPath))
                        {
                            return HTTPInternalServerError;
                        }

                        int nWritten = snprintf (
                            PNPage, sizeof PNPage,
                            "<html>"
                            "<head><title>Copy complete: PN-JV80/%s</title></head>"
                            "<body>"
                            "<h1>Copy complete: PN-JV80/%s</h1>"
                            "<p>The file has been copied.</p>"
                            "<ul>"
                            "<li>File name: %s</li>"
                            "<li>Source folder: %s</li>"
                            "<li>Destination folder: %s</li>"
                            "<li>Source SD path: %s</li>"
                            "<li>Destination SD path: %s</li>"
                            "<li>File size before copy: %s</li>"
                            "</ul>"
                            "<p><a href=\"/browse/PN-JV80/%s/%s\">Open copied file detail</a></p>"
                            "<p><a href=\"/browse/PN-JV80/%s\">Back to destination folder</a></p>"
                            "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                            "<p><a href=\"/browse\">Back to browse root</a></p>"
                            "<p><a href=\"/\">Back to home</a></p>"
                            "</body>"
                            "</html>",
                            ItemName,
                            ItemName,
                            pChildName,
                            pParentName,
                            RequestedDest,
                            FullPath,
                            TargetPath,
                            SizeText,
                            EncodedDest,
                            EncodedChild,
                            EncodedDest);

                        if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                        {
                            return HTTPInternalServerError;
                        }

                        pBody = PNPage;
                    }

                    else if (strncmp (pPath, "/move-exec/PN-JV80/", 19) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 19;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        char SizeText[64];
                        if (fseek (pInput, 0, SEEK_END) == 0)
                        {
                            long nFileSize = ftell (pInput);
                            if (nFileSize >= 0)
                            {
                                snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                            }
                            else
                            {
                                snprintf (SizeText, sizeof SizeText, "unknown");
                            }
                        }
                        else
                        {
                            snprintf (SizeText, sizeof SizeText, "unknown");
                        }

                        fclose (pInput);

                        bool bHaveDestParam = false;
                        bool bRequestedDestInvalid = false;
                        char RequestedDest[256];
                        RequestedDest[0] = '\0';

                        if (pParams != 0 && pParams[0] != '\0')
                        {
                            const char *pParam = pParams;

                            while (*pParam != '\0')
                            {
                                const char *pNext = strchr (pParam, '&');
                                size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

                                if (nParamLen >= 5 && strncmp (pParam, "dest=", 5) == 0)
                                {
                                    bHaveDestParam = true;

                                    char RawValue[256];
                                    size_t nValueLen = nParamLen - 5;
                                    if (nValueLen >= sizeof RawValue)
                                    {
                                        return HTTPInternalServerError;
                                    }

                                    for (size_t i = 0; i < nValueLen; i++)
                                    {
                                        char ch = pParam[5 + i];
                                        RawValue[i] = ch == '+' ? ' ' : ch;
                                    }

                                    RawValue[nValueLen] = '\0';

                                    if (!URLDecode (RawValue, RequestedDest, sizeof RequestedDest))
                                    {
                                        bRequestedDestInvalid = true;
                                        RequestedDest[0] = '\0';
                                    }

                                    break;
                                }

                                if (pNext == 0)
                                {
                                    break;
                                }

                                pParam = pNext + 1;
                            }
                        }

                        if (!bHaveDestParam
                            || RequestedDest[0] == '\0'
                            || bRequestedDestInvalid
                            || HasSlash (RequestedDest)
                            || IsDotName (RequestedDest)
                            || strcmp (RequestedDest, pParentName) == 0)
                        {
                            char InvalidEncodedParent[768];
                            char InvalidEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, InvalidEncodedParent, sizeof InvalidEncodedParent)
                                || !URLEncodePathSegment (pChildName, InvalidEncodedChild, sizeof InvalidEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nInvalidWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Move not executed</title></head>"
                                "<body>"
                                "<h1>Move not executed</h1>"
                                "<p>Invalid destination subfolder. Use an existing subfolder name different from the source folder.</p>"
                                "<ul>"
                                "<li>File name: %s</li>"
                                "<li>Source folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/move/PN-JV80/%s/%s\">Back to move page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                InvalidEncodedParent,
                                InvalidEncodedChild,
                                InvalidEncodedParent,
                                InvalidEncodedChild);

                            if (nInvalidWritten < 0 || (unsigned) nInvalidWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }

                        char TargetFolderPath[512];
                        int nTargetFolderWritten = snprintf (
                            TargetFolderPath, sizeof TargetFolderPath,
                            "SD:/PN-JV80/%s", RequestedDest);

                        if (nTargetFolderWritten < 0 || (unsigned) nTargetFolderWritten >= sizeof TargetFolderPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTargetDir = opendir (TargetFolderPath);
                        if (pTargetDir == 0)
                        {
                            char MissingEncodedParent[768];
                            char MissingEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, MissingEncodedParent, sizeof MissingEncodedParent)
                                || !URLEncodePathSegment (pChildName, MissingEncodedChild, sizeof MissingEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nMissingWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Move not executed</title></head>"
                                "<body>"
                                "<h1>Move not executed</h1>"
                                "<p>Destination subfolder does not exist. No file has been modified.</p>"
                                "<ul>"
                                "<li>File name: %s</li>"
                                "<li>Source folder: %s</li>"
                                "<li>Requested destination folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/move/PN-JV80/%s/%s\">Back to move page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                RequestedDest,
                                MissingEncodedParent,
                                MissingEncodedChild,
                                MissingEncodedParent,
                                MissingEncodedChild);

                            if (nMissingWritten < 0 || (unsigned) nMissingWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }
                        closedir (pTargetDir);

                        char TargetPath[768];
                        int nTargetWritten = snprintf (
                            TargetPath, sizeof TargetPath,
                            "SD:/PN-JV80/%s/%s", RequestedDest, pChildName);

                        if (nTargetWritten < 0 || (unsigned) nTargetWritten >= sizeof TargetPath)
                        {
                            return HTTPInternalServerError;
                        }

                        bool bTargetExists = false;

                        DIR *pTargetTestDir = opendir (TargetPath);
                        if (pTargetTestDir != 0)
                        {
                            closedir (pTargetTestDir);
                            bTargetExists = true;
                        }

                        if (!bTargetExists)
                        {
                            FILE *pTargetInput = fopen (TargetPath, "rb");
                            if (pTargetInput != 0)
                            {
                                fclose (pTargetInput);
                                bTargetExists = true;
                            }
                        }

                        if (bTargetExists)
                        {
                            char ConflictEncodedParent[768];
                            char ConflictEncodedChild[768];
                            char ConflictEncodedDest[768];

                            if (!URLEncodePathSegment (pParentName, ConflictEncodedParent, sizeof ConflictEncodedParent)
                                || !URLEncodePathSegment (pChildName, ConflictEncodedChild, sizeof ConflictEncodedChild)
                                || !URLEncodePathSegment (RequestedDest, ConflictEncodedDest, sizeof ConflictEncodedDest))
                            {
                                return HTTPInternalServerError;
                            }

                            int nConflictWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Move not executed</title></head>"
                                "<body>"
                                "<h1>Move not executed</h1>"
                                "<p>Target file already exists in destination folder. No file has been modified.</p>"
                                "<ul>"
                                "<li>File name: %s</li>"
                                "<li>Source folder: %s</li>"
                                "<li>Destination folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/move/PN-JV80/%s/%s\">Back to move page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s\">Back to destination folder</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                RequestedDest,
                                ConflictEncodedParent,
                                ConflictEncodedChild,
                                ConflictEncodedParent,
                                ConflictEncodedChild,
                                ConflictEncodedDest);

                            if (nConflictWritten < 0 || (unsigned) nConflictWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;   
                        }

                        char EncodedDest[768];
                        char EncodedChild[768];
                        if (!URLEncodePathSegment (RequestedDest, EncodedDest, sizeof EncodedDest)
                            || !URLEncodePathSegment (pChildName, EncodedChild, sizeof EncodedChild))
                        {
                            return HTTPInternalServerError;
                        }

                        if (rename (FullPath, TargetPath) != 0)
                        {
                            return HTTPInternalServerError;
                        }

                        int nWritten = snprintf (
                            PNPage, sizeof PNPage,
                            "<html>"
                            "<head><title>Move complete: PN-JV80/%s</title></head>"
                            "<body>"
                            "<h1>Move complete: PN-JV80/%s</h1>"
                            "<p>The file has been moved.</p>"
                            "<ul>"
                            "<li>File name: %s</li>"
                            "<li>Previous folder: %s</li>"
                            "<li>New folder: %s</li>"
                            "<li>Previous SD path: %s</li>"
                            "<li>New SD path: %s</li>"
                            "<li>File size before move: %s</li>"
                            "</ul>"
                            "<p><a href=\"/browse/PN-JV80/%s/%s\">Open moved file detail</a></p>"
                            "<p><a href=\"/browse/PN-JV80/%s\">Back to destination folder</a></p>"
                            "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                            "<p><a href=\"/browse\">Back to browse root</a></p>"
                            "<p><a href=\"/\">Back to home</a></p>"
                            "</body>"
                            "</html>",
                            ItemName,
                            ItemName,
                            pChildName,
                            pParentName,
                            RequestedDest,
                            FullPath,
                            TargetPath,
                            SizeText,
                            EncodedDest,
                            EncodedChild,
                            EncodedDest);

                        if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                        {
                            return HTTPInternalServerError;
                        }

                        pBody = PNPage;
                    }

                    else if (strncmp (pPath, "/move/PN-JV80/", 14) == 0)
                    {
                        THTTPStatus Status = HandleMovePage (
                            m_Config,
                            pPath,
                            pParams,
                            PNPage,
                            sizeof PNPage,
                            &pBody);

                        if (Status != HTTPOK)
                        {
                            return Status;
                        }
                    }

                    else if (strncmp (pPath, "/rename/PN-JV80/", 16) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 16;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        char SizeText[64];
                        if (fseek (pInput, 0, SEEK_END) == 0)
                        {
                            long nFileSize = ftell (pInput);
                            if (nFileSize >= 0)
                            {
                                snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                            }
                            else
                            {
                                snprintf (SizeText, sizeof SizeText, "unknown");
                            }
                        }
                        else
                        {
                            snprintf (SizeText, sizeof SizeText, "unknown");
                        }

                        fclose (pInput);

                        char EncodedParent[768];
                        char EncodedChild[768];
                        if (!URLEncodePathSegment (pParentName, EncodedParent, sizeof EncodedParent)
                            || !URLEncodePathSegment (pChildName, EncodedChild, sizeof EncodedChild))
                        {
                            return HTTPInternalServerError;
                        }

                        bool bHaveNewNameParam = false;
                        bool bRequestedNameInvalid = false;
                        char RequestedName[256];
                        RequestedName[0] = '\0';

                        if (pParams != 0 && pParams[0] != '\0')
                        {
                            const char *pParam = pParams;

                            while (*pParam != '\0')
                            {
                                const char *pNext = strchr (pParam, '&');
                                size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

                                if (nParamLen >= 8 && strncmp (pParam, "newname=", 8) == 0)
                                {
                                    bHaveNewNameParam = true;

                                    char RawValue[512];
                                    size_t nValueLen = nParamLen - 8;
                                    if (nValueLen >= sizeof RawValue)
                                    {
                                        return HTTPInternalServerError;
                                    }

                                    for (size_t i = 0; i < nValueLen; i++)
                                    {
                                        char ch = pParam[8 + i];
                                        RawValue[i] = ch == '+' ? ' ' : ch;
                                    }

                                    RawValue[nValueLen] = '\0';

                                    if (!URLDecode (RawValue, RequestedName, sizeof RequestedName))
                                    {
                                        bRequestedNameInvalid = true;
                                        RequestedName[0] = '\0';
                                    }

                                    break;
                                }

                                if (pNext == 0)
                                {
                                    break;
                                }

                                pParam = pNext + 1;
                            }
                        }

                        const char *pRenameInfo = "Enter a new file name and submit to preview the requested target. Rename execution is not enabled yet.";
                        char PreviewSection[1600];
                        PreviewSection[0] = '\0';

                        if (bHaveNewNameParam)
                        {
                            if (RequestedName[0] == '\0'
                                || bRequestedNameInvalid
                                || HasSlash (RequestedName)
                                || IsDotName (RequestedName))
                            {
                                pRenameInfo = "The requested new file name is not valid. Rename execution is not enabled yet.";

                                int nPreviewWritten = snprintf (
                                    PreviewSection, sizeof PreviewSection,
                                    "<h2>Requested target</h2>"
                                    "<p>Invalid file name. Empty names, slashes and dot names are not allowed.</p>");

                                if (nPreviewWritten < 0 || (unsigned) nPreviewWritten >= sizeof PreviewSection)
                                {
                                    return HTTPInternalServerError;
                                }
                            }
                            else
                            {
                                char EncodedRequested[768];
                                if (!URLEncodePathSegment (RequestedName, EncodedRequested, sizeof EncodedRequested))
                                {
                                    return HTTPInternalServerError;
                                }

                                pRenameInfo = "Preview only. Continue to rename confirmation when ready. Rename execution is not enabled yet.";

                                int nPreviewWritten = snprintf (
                                    PreviewSection, sizeof PreviewSection,
                                    "<h2>Requested target</h2>"
                                    "<ul>"
                                    "<li>Requested new file name: %s</li>"
                                    "<li>Target SD path: SD:/PN-JV80/%s/%s</li>"
                                    "</ul>"
                                    "<p><a href=\"/rename-confirm/PN-JV80/%s/%s?newname=%s\">Continue to rename confirmation</a></p>",
                                    RequestedName,
                                    pParentName,
                                    RequestedName,
                                    EncodedParent,
                                    EncodedChild,
                                    EncodedRequested);

                                if (nPreviewWritten < 0 || (unsigned) nPreviewWritten >= sizeof PreviewSection)
                                {
                                    return HTTPInternalServerError;
                                }
                            }  
                        }

                        int nWritten = snprintf (
                            PNPage, sizeof PNPage,
                            "<html>"
                            "<head><title>Rename: PN-JV80/%s</title></head>"
                            "<body>"
                            "<h1>Rename: PN-JV80/%s</h1>"
                            "<p>Preparation page for a future rename action. No file has been modified.</p>"
                            "<ul>"
                            "<li>Current file name: %s</li>"
                            "<li>SD path: %s</li>"
                            "<li>File size: %s</li>"
                            "</ul>"
                            "<form method=\"get\" action=\"/rename/PN-JV80/%s/%s\">"
                            "<p>New file name: <input type=\"text\" name=\"newname\" size=\"40\"></p>"
                            "<p><input type=\"submit\" value=\"Preview rename target\"></p>"
                            "</form>"
                            "%s"
                            "<p>%s</p>"
                            "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                            "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                            "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                            "<p><a href=\"/browse\">Back to browse root</a></p>"
                            "<p><a href=\"/\">Back to home</a></p>"
                            "</body>"
                            "</html>",
                            ItemName,
                            ItemName,
                            ItemName,
                            FullPath,
                            SizeText,
                            EncodedParent,
                            EncodedChild,
                            PreviewSection,
                            pRenameInfo,
                            EncodedParent,
                            EncodedChild,
                            EncodedParent);

                        if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                        {
                            return HTTPInternalServerError;
                        }

                        pBody = PNPage;
                    }

                    else if (strncmp (pPath, "/rename-confirm/PN-JV80/", 24) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 24;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        char SizeText[64];
                        if (fseek (pInput, 0, SEEK_END) == 0)
                        {
                            long nFileSize = ftell (pInput);
                            if (nFileSize >= 0)
                            {
                                snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                            }
                            else
                            {
                                snprintf (SizeText, sizeof SizeText, "unknown");
                            }
                        }
                        else
                        {
                            snprintf (SizeText, sizeof SizeText, "unknown");
                        }

                        fclose (pInput);

                        bool bHaveNewNameParam = false;
                        bool bRequestedNameInvalid = false;
                        char RequestedName[256];
                        RequestedName[0] = '\0';

                        if (pParams != 0 && pParams[0] != '\0')
                        {
                            const char *pParam = pParams;

                            while (*pParam != '\0')
                            {
                                const char *pNext = strchr (pParam, '&');
                                size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

                                if (nParamLen >= 8 && strncmp (pParam, "newname=", 8) == 0)
                                {
                                    bHaveNewNameParam = true;

                                    char RawValue[512];
                                    size_t nValueLen = nParamLen - 8;
                                    if (nValueLen >= sizeof RawValue)
                                    {
                                        return HTTPInternalServerError;
                                    }

                                    for (size_t i = 0; i < nValueLen; i++)
                                    {
                                        char ch = pParam[8 + i];
                                        RawValue[i] = ch == '+' ? ' ' : ch;
                                    }

                                    RawValue[nValueLen] = '\0';

                                    if (!URLDecode (RawValue, RequestedName, sizeof RequestedName))
                                    {
                                        bRequestedNameInvalid = true;
                                        RequestedName[0] = '\0';
                                    }

                                    break;
                                }

                                if (pNext == 0)
                                {
                                    break;
                                }

                                pParam = pNext + 1;
                            }
                        }

                        if (!bHaveNewNameParam
                            || RequestedName[0] == '\0'
                            || bRequestedNameInvalid
                            || HasSlash (RequestedName)
                            || IsDotName (RequestedName))
                        {
                            char InvalidEncodedParent[768];
                            char InvalidEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, InvalidEncodedParent, sizeof InvalidEncodedParent)
                                || !URLEncodePathSegment (pChildName, InvalidEncodedChild, sizeof InvalidEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nInvalidWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Rename not executed</title></head>"
                                "<body>"
                                "<h1>Rename not executed</h1>"
                                "<p>Invalid new file name. Use a valid .syx file name without slashes.</p>"
                                "<ul>"
                                "<li>Current file name: %s</li>"
                                "<li>Current folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/rename/PN-JV80/%s/%s\">Back to rename page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                InvalidEncodedParent,
                                InvalidEncodedChild,
                                InvalidEncodedParent,
                                InvalidEncodedChild);

                            if (nInvalidWritten < 0 || (unsigned) nInvalidWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }

                        char EncodedParent[768];
                        char EncodedChild[768];
                        char EncodedRequested[768];

                        if (!URLEncodePathSegment (pParentName, EncodedParent, sizeof EncodedParent)
                            || !URLEncodePathSegment (pChildName, EncodedChild, sizeof EncodedChild)
                            || !URLEncodePathSegment (RequestedName, EncodedRequested, sizeof EncodedRequested))
                        {
                            return HTTPInternalServerError;
                        }

                        int nWritten = snprintf (
                            PNPage, sizeof PNPage,
                            "<html>"
                            "<head><title>Rename confirm: PN-JV80/%s</title></head>"
                            "<body>"
                            "<h1>Rename confirm: PN-JV80/%s</h1>"
                            "<p>This action will rename the file. No file has been modified yet.</p>"
                            "<ul>"
                            "<li>Current file name: %s</li>"
                            "<li>Requested new file name: %s</li>"
                            "<li>Current SD path: %s</li>"
                            "<li>Requested target path: SD:/PN-JV80/%s/%s</li>"
                            "<li>File size: %s</li>"
                            "</ul>"
                            "<p><a href=\"/rename-exec/PN-JV80/%s/%s?newname=%s\">Confirm rename now</a></p>"
                            "<p><a href=\"/rename/PN-JV80/%s/%s?newname=%s\">Back to rename page</a></p>"
                            "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                            "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                            "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                            "<p><a href=\"/browse\">Back to browse root</a></p>"
                            "<p><a href=\"/\">Back to home</a></p>"
                            "</body>"
                            "</html>",
                            ItemName,
                            ItemName,
                            pChildName,
                            RequestedName,
                            FullPath,
                            pParentName,
                            RequestedName,
                            SizeText,
                            EncodedParent,
                            EncodedChild,
                            EncodedRequested,
                            EncodedParent,
                            EncodedChild,
                            EncodedRequested,
                            EncodedParent,
                            EncodedChild,
                            EncodedParent);

                        if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                        {
                            return HTTPInternalServerError;
                        }

                        pBody = PNPage;
                    }

                    else if (strncmp (pPath, "/rename-exec/PN-JV80/", 21) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 21;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        char SizeText[64];
                        if (fseek (pInput, 0, SEEK_END) == 0)
                        {
                            long nFileSize = ftell (pInput);
                            if (nFileSize >= 0)
                            {
                                snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                            }
                            else
                            {
                                snprintf (SizeText, sizeof SizeText, "unknown");
                            }
                        }
                        else
                        {
                            snprintf (SizeText, sizeof SizeText, "unknown");
                        }

                        fclose (pInput);

                        bool bHaveNewNameParam = false;
                        bool bRequestedNameInvalid = false;
                        char RequestedName[256];
                        RequestedName[0] = '\0';

                        if (pParams != 0 && pParams[0] != '\0')
                        {
                            const char *pParam = pParams;

                            while (*pParam != '\0')
                            {
                                const char *pNext = strchr (pParam, '&');
                                size_t nParamLen = pNext != 0 ? (size_t) (pNext - pParam) : strlen (pParam);

                                if (nParamLen >= 8 && strncmp (pParam, "newname=", 8) == 0)
                                {
                                    bHaveNewNameParam = true;

                                    char RawValue[512];
                                    size_t nValueLen = nParamLen - 8;
                                    if (nValueLen >= sizeof RawValue)
                                    {
                                        return HTTPInternalServerError;
                                    }

                                    for (size_t i = 0; i < nValueLen; i++)
                                    {
                                        char ch = pParam[8 + i];
                                        RawValue[i] = ch == '+' ? ' ' : ch;
                                    }

                                    RawValue[nValueLen] = '\0';

                                    if (!URLDecode (RawValue, RequestedName, sizeof RequestedName))
                                    {
                                        bRequestedNameInvalid = true;
                                        RequestedName[0] = '\0';
                                    }

                                    break;
                                }

                                if (pNext == 0)
                                {
                                    break;
                                }

                                pParam = pNext + 1;
                            }
                        }

                        if (!bHaveNewNameParam
                            || RequestedName[0] == '\0'
                            || bRequestedNameInvalid
                            || HasSlash (RequestedName)
                            || IsDotName (RequestedName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pNewExt = strrchr (RequestedName, '.');
                        if (pNewExt == 0
                            || !((pNewExt[1] == 's' || pNewExt[1] == 'S')
                              && (pNewExt[2] == 'y' || pNewExt[2] == 'Y')
                              && (pNewExt[3] == 'x' || pNewExt[3] == 'X')
                              && pNewExt[4] == '\0'))
                        {
                            char InvalidExtEncodedParent[768];
                            char InvalidExtEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, InvalidExtEncodedParent, sizeof InvalidExtEncodedParent)
                                || !URLEncodePathSegment (pChildName, InvalidExtEncodedChild, sizeof InvalidExtEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nInvalidExtWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Rename not executed</title></head>"
                                "<body>"
                                "<h1>Rename not executed</h1>"
                                "<p>Invalid new file name. The new name must end with .syx.</p>"
                                "<ul>"
                                "<li>Current file name: %s</li>"
                                "<li>Current folder: %s</li>"
                                "<li>Requested new file name: %s</li>"
                                "</ul>"
                                "<p><a href=\"/rename/PN-JV80/%s/%s\">Back to rename page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                RequestedName,
                                InvalidExtEncodedParent,
                                InvalidExtEncodedChild,
                                InvalidExtEncodedParent,
                                InvalidExtEncodedChild);

                            if (nInvalidExtWritten < 0 || (unsigned) nInvalidExtWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }

                        if (strcmp (RequestedName, pChildName) == 0)
                        {
                            char SameEncodedParent[768];
                            char SameEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, SameEncodedParent, sizeof SameEncodedParent)
                                || !URLEncodePathSegment (pChildName, SameEncodedChild, sizeof SameEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nSameWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Rename not executed</title></head>"
                                "<body>"
                                "<h1>Rename not executed</h1>"
                                "<p>The requested new file name is identical to the current file name. No file has been modified.</p>"
                                "<ul>"
                                "<li>Current file name: %s</li>"
                                "<li>Current folder: %s</li>"
                                "</ul>"
                                "<p><a href=\"/rename/PN-JV80/%s/%s\">Back to rename page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                SameEncodedParent,
                                SameEncodedChild,
                                SameEncodedParent,
                                SameEncodedChild);

                            if (nSameWritten < 0 || (unsigned) nSameWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }

                        char TargetPath[768];
                        int nTargetWritten = snprintf (
                            TargetPath, sizeof TargetPath,
                            "SD:/PN-JV80/%s/%s", pParentName, RequestedName);

                        if (nTargetWritten < 0 || (unsigned) nTargetWritten >= sizeof TargetPath)
                        {
                            return HTTPInternalServerError;
                        }

                        bool bTargetExists = false;

                        DIR *pTargetDir = opendir (TargetPath);
                        if (pTargetDir != 0)
                        {
                            closedir (pTargetDir);
                            bTargetExists = true;
                        }

                        if (!bTargetExists)
                        {
                            FILE *pTargetInput = fopen (TargetPath, "rb");
                            if (pTargetInput != 0)
                            {
                                fclose (pTargetInput);
                                bTargetExists = true;
                            }
                        }

                        if (bTargetExists)
                        {
                            char ConflictEncodedParent[768];
                            char ConflictEncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, ConflictEncodedParent, sizeof ConflictEncodedParent)
                                || !URLEncodePathSegment (pChildName, ConflictEncodedChild, sizeof ConflictEncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nConflictWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Rename not executed</title></head>"
                                "<body>"
                                "<h1>Rename not executed</h1>"
                                "<p>A file with the requested new name already exists in this folder. No file has been modified.</p>"
                                "<ul>"
                                "<li>Current file name: %s</li>"
                                "<li>Current folder: %s</li>"
                                "<li>Requested new file name: %s</li>"
                                "</ul>"
                                "<p><a href=\"/rename/PN-JV80/%s/%s\">Back to rename page</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                RequestedName,
                                ConflictEncodedParent,
                                ConflictEncodedChild,
                                ConflictEncodedParent,
                                ConflictEncodedChild,
                                ConflictEncodedParent);

                            if (nConflictWritten < 0 || (unsigned) nConflictWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }

                        if (rename (FullPath, TargetPath) != 0)
                        {
                            return HTTPInternalServerError;
                        }

                        char EncodedParent[768];
                        if (!URLEncodePathSegment (pParentName, EncodedParent, sizeof EncodedParent))
                        {
                            return HTTPInternalServerError;
                        }

                        int nWritten = snprintf (
                            PNPage, sizeof PNPage,
                            "<html>"
                            "<head><title>Rename complete: PN-JV80/%s</title></head>"
                            "<body>"
                            "<h1>Rename complete: PN-JV80/%s</h1>"
                            "<p>The file has been renamed.</p>"
                            "<ul>"
                            "<li>Previous file name: %s</li>"
                            "<li>New file name: %s</li>"
                            "<li>Previous SD path: %s</li>"
                            "<li>New SD path: %s</li>"
                            "<li>File size before rename: %s</li>"
                            "</ul>"
                            "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                            "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                            "<p><a href=\"/browse\">Back to browse root</a></p>"
                            "<p><a href=\"/\">Back to home</a></p>"
                            "</body>"
                            "</html>",
                            ItemName,
                            ItemName,
                            pChildName,
                            RequestedName,
                            FullPath,
                            TargetPath,
                            SizeText,
                            EncodedParent);

                        if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                        {
                            return HTTPInternalServerError;
                        }

                        pBody = PNPage;
                    }

                    else if (strncmp (pPath, "/delete-exec/PN-JV80/", 21) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 21;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        char SizeText[64];
                        if (fseek (pInput, 0, SEEK_END) == 0)
                        {
                            long nFileSize = ftell (pInput);
                            if (nFileSize >= 0)
                            {
                                snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                            }
                            else
                            {
                                snprintf (SizeText, sizeof SizeText, "unknown");
                            }
                        }
                        else
                        {
                            snprintf (SizeText, sizeof SizeText, "unknown");
                        }

                        fclose (pInput);

                        char EncodedParent[768];
                        if (!URLEncodePathSegment (pParentName, EncodedParent, sizeof EncodedParent))
                        {
                            return HTTPInternalServerError;
                        }

                        if (remove (FullPath) != 0)
                        {
                            return HTTPInternalServerError;
                        }

                        int nWritten = snprintf (
                            PNPage, sizeof PNPage,
                            "<html>"
                            "<head><title>Delete complete: PN-JV80/%s</title></head>"
                            "<body>"
                            "<h1>Delete complete: PN-JV80/%s</h1>"
                            "<p>The file has been deleted. No undo is available.</p>"
                            "<ul>"
                            "<li>File name: %s</li>"
                            "<li>Former SD path: %s</li>"
                            "<li>Former file size: %s</li>"
                            "</ul>"
                            "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                            "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                            "<p><a href=\"/browse\">Back to browse root</a></p>"
                            "<p><a href=\"/\">Back to home</a></p>"
                            "</body>"
                            "</html>",
                            ItemName,
                            ItemName,
                            ItemName,
                            FullPath,
                            SizeText,
                            EncodedParent);

                        if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                        {
                            return HTTPInternalServerError;
                        }

                        pBody = PNPage;
                    }

                    else if (strncmp (pPath, "/delete/PN-JV80/", 16) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 16;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        char SizeText[64];
                        if (fseek (pInput, 0, SEEK_END) == 0)
                        {
                            long nFileSize = ftell (pInput);
                            if (nFileSize >= 0)
                            {
                                snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
                            }
                            else
                            {
                                snprintf (SizeText, sizeof SizeText, "unknown");
                            }
                        }
                        else
                        {
                            snprintf (SizeText, sizeof SizeText, "unknown");
                        }

                        fclose (pInput);

                        char EncodedParent[768];
                        char EncodedChild[768];
                        if (!URLEncodePathSegment (pParentName, EncodedParent, sizeof EncodedParent)
                            || !URLEncodePathSegment (pChildName, EncodedChild, sizeof EncodedChild))
                        {
                            return HTTPInternalServerError;
                        }

                        int nWritten = snprintf (
                            PNPage, sizeof PNPage,
                            "<html>"
                            "<head><title>Delete confirm: PN-JV80/%s</title></head>"
                            "<body>"
                            "<h1>Delete confirm: PN-JV80/%s</h1>"
                            "<p>This action will permanently delete the file. No file has been modified yet.</p>"
                            "<ul>"
                            "<li>File name: %s</li>"
                            "<li>SD path: %s</li>"
                            "<li>File size: %s</li>"
                            "</ul>"
                            "<p><a href=\"/delete-exec/PN-JV80/%s/%s\">Confirm delete now</a></p>"
                            "<p><a href=\"/browse/PN-JV80/%s/%s\">Cancel and go back to file detail</a></p>"
                            "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                            "<p><a href=\"/browse\">Back to browse root</a></p>"
                            "<p><a href=\"/\">Back to home</a></p>"
                            "</body>"
                            "</html>",
                            ItemName,
                            ItemName,
                            ItemName,
                            FullPath,
                            SizeText,
                            EncodedParent,
                            EncodedChild,
                            EncodedParent,
                            EncodedChild);

                        if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
                        {
                            return HTTPInternalServerError;
                        }

                        pBody = PNPage;
                    }     
     
                    else if (strncmp (pPath, "/download/PN-JV80/", 18) == 0)
                    {
                        if (!m_Config.m_bExposePNJV80)
                        {
                            return HTTPNotFound;
                        }

                        const char *pEncodedName = pPath + 18;
                        if (pEncodedName == 0 || pEncodedName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        char ItemPath[512];
                        if (!URLDecode (pEncodedName, ItemPath, sizeof ItemPath))
                        {
                            return HTTPNotFound;
                        }

                        if (ItemPath[0] == '\0' || strchr (ItemPath, '\\') != 0)
                        {
                            return HTTPNotFound;
                        }

                        unsigned nSlashCount = 0;
                        for (const char *pScan = ItemPath; *pScan != '\0'; pScan++)
                        {
                            if (*pScan == '/')
                            {
                                nSlashCount++;
                            }
                        }

                        if (nSlashCount != 1)
                        {
                            return HTTPNotFound;
                        }

                        char ItemName[512];
                        int nItemWritten = snprintf (ItemName, sizeof ItemName, "%s", ItemPath);
                        if (nItemWritten < 0 || (unsigned) nItemWritten >= sizeof ItemName)
                        {
                            return HTTPInternalServerError;
                        }

                        char *pSlash = strchr (ItemPath, '/');
                        if (pSlash == 0)
                        {
                            return HTTPNotFound;
                        }

                        *pSlash = '\0';

                        const char *pParentName = ItemPath;
                        const char *pChildName = pSlash + 1;

                        if (pParentName[0] == '\0' || pChildName[0] == '\0')
                        {
                            return HTTPNotFound;
                        }

                        if (IsDotName (pParentName) || IsDotName (pChildName))
                        {
                            return HTTPNotFound;
                        }

                        const char *pExt = strrchr (pChildName, '.');
                        if (pExt == 0
                            || !((pExt[1] == 's' || pExt[1] == 'S')
                              && (pExt[2] == 'y' || pExt[2] == 'Y')
                              && (pExt[3] == 'x' || pExt[3] == 'X')
                              && pExt[4] == '\0'))
                        {
                            return HTTPNotFound;
                        }

                        char FullPath[768];
                        int nPathWritten = snprintf (
                            FullPath, sizeof FullPath,
                            "SD:/PN-JV80/%s", ItemName);

                        if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
                        {
                            return HTTPInternalServerError;
                        }

                        DIR *pTestDir = opendir (FullPath);
                        if (pTestDir != 0)
                        {
                            closedir (pTestDir);
                            return HTTPNotFound;
                        }

                        FILE *pInput = fopen (FullPath, "rb");
                        if (pInput == 0)
                        {
                            return HTTPNotFound;
                        }

                        if (fseek (pInput, 0, SEEK_END) != 0)
                        {
                            fclose (pInput);
                            return HTTPInternalServerError;
                        }

                        long nFileSize = ftell (pInput);
                        if (nFileSize < 0)
                        {
                            fclose (pInput);
                            return HTTPInternalServerError;
                        }

                        if ((unsigned long) nFileSize > *pLength)
                        {
                            fclose (pInput);

                            char EncodedParent[768];
                            char EncodedChild[768];

                            if (!URLEncodePathSegment (pParentName, EncodedParent, sizeof EncodedParent)
                                || !URLEncodePathSegment (pChildName, EncodedChild, sizeof EncodedChild))
                            {
                                return HTTPInternalServerError;
                            }

                            int nTooLargeWritten = snprintf (
                                PNPage, sizeof PNPage,
                                "<html>"
                                "<head><title>Download unavailable</title></head>"
                                "<body>"
                                "<h1>Download unavailable</h1>"
                                "<p>This file exceeds the current HTTP download buffer limit. No file has been modified.</p>"
                                "<ul>"
                                "<li>File name: %s</li>"
                                "<li>Source folder: %s</li>"
                                "<li>File size: %ld bytes</li>"
                                "<li>Current HTTP buffer limit: %u bytes</li>"
                                "</ul>"
                                "<p><a href=\"/browse/PN-JV80/%s/%s\">Back to file detail</a></p>"
                                "<p><a href=\"/browse/PN-JV80/%s\">Back to folder</a></p>"
                                "<p><a href=\"/browse/PN-JV80\">Back to PN-JV80</a></p>"
                                "<p><a href=\"/browse\">Back to browse root</a></p>"
                                "<p><a href=\"/\">Back to home</a></p>"
                                "</body>"
                                "</html>",
                                pChildName,
                                pParentName,
                                nFileSize,
                                *pLength,
                                EncodedParent,
                                EncodedChild,
                                EncodedParent);

                            if (nTooLargeWritten < 0 || (unsigned) nTooLargeWritten >= sizeof PNPage)
                            {
                                return HTTPInternalServerError;
                            }

                            size_t nBodyLength = strlen (PNPage);
                            if (nBodyLength > *pLength)
                            {
                                return HTTPInternalServerError;
                            }

                            memcpy (pBuffer, PNPage, nBodyLength);
                            *pLength = (unsigned) nBodyLength;
                            *ppContentType = "text/html";
                            return HTTPOK;
                        }

                        if (fseek (pInput, 0, SEEK_SET) != 0)
                        {
                            fclose (pInput);
                            return HTTPInternalServerError;
                        }

                        if (nFileSize > 0)
                        {
                            size_t nRead = fread (pBuffer, 1, (size_t) nFileSize, pInput);
                            if (nRead != (size_t) nFileSize)
                            {
                                fclose (pInput);
                                return HTTPInternalServerError;
                            }
                        }

                        fclose (pInput);

                        *pLength = (unsigned) nFileSize;
                        *ppContentType = "application/octet-stream";
                        return HTTPOK;
                    }           
           
                    else if (strcmp (pPath, "/browse/roms") == 0)
                    {
	                if (!m_Config.m_bExposeRoms)
	                {
		            return HTTPNotFound;
	                }

	                DIR *pDir = opendir ("SD:/roms");

	                size_t nUsed = 0;
	                int nWritten = snprintf (
		            PNPage, sizeof PNPage,
		            "<html>"
		            "<head><title>Browse: roms</title></head>"
                            "<body>"
                            "<h1>Browse: roms</h1>"
		            "<p>Read-only file listing. Click a file for details. No download or write operations are enabled.</p>"
		            "<ul>");

	                if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
	                {
		            return HTTPInternalServerError;
	                }

	                nUsed = (size_t) nWritten;

	                if (pDir == 0)
	                {
		            nWritten = snprintf (
			        PNPage + nUsed, sizeof PNPage - nUsed,
			        "<li>Cannot open directory: SD:/roms</li>");

		            if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
		            {
			        return HTTPInternalServerError;
		            }

		            nUsed += (size_t) nWritten;
	                }
	                     
	                else
	                {
		            char Names[64][256];
		            unsigned nNames = 0;
		            bool bTruncated = false;

		            for (;;)
		            {
			        struct dirent *pEntry = readdir (pDir);
			        if (pEntry == 0)
			        {
				    break;
			        }

			        if (strcmp (pEntry->d_name, ".") == 0 || strcmp (pEntry->d_name, "..") == 0)
			        {
				    continue;
			        }

			        if (nNames >= 64)
			        {
				    bTruncated = true;
				    break;
			        }

			        int nCopyWritten = snprintf (
				    Names[nNames], sizeof Names[nNames],
				    "%s",
				    pEntry->d_name);

			        if (nCopyWritten < 0 || (unsigned) nCopyWritten >= sizeof Names[nNames])
			        {
				    closedir (pDir);
				    return HTTPInternalServerError;
			        }

			        nNames++;
		            }

		            closedir (pDir);

		            if (nNames > 1)
		            {
			        qsort (Names, nNames, sizeof Names[0], CompareNameRows);
		            }

		            for (unsigned i = 0; i < nNames; i++)
		            {
			        char EncodedName[768];
			        if (!URLEncodePathSegment (Names[i], EncodedName, sizeof EncodedName))
			        {
				    return HTTPInternalServerError;
			        }

			        nWritten = snprintf (
				    PNPage + nUsed, sizeof PNPage - nUsed,
				    "<li><a href=\"/browse/roms/%s\">%s</a></li>",
				    EncodedName,
				    Names[i]);

			        if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
			        {
				    return HTTPInternalServerError;
			        }

			        nUsed += (size_t) nWritten;
		            }

		            if (nNames == 0)
		            {
			        nWritten = snprintf (
				    PNPage + nUsed, sizeof PNPage - nUsed,
				    "<li>(empty)</li>");

			        if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
			        {
				    return HTTPInternalServerError;
			        }

			        nUsed += (size_t) nWritten;
		            }

		            if (bTruncated)
		            {
			        nWritten = snprintf (
				    PNPage + nUsed, sizeof PNPage - nUsed,
				    "<li>(listing truncated after 64 entries)</li>");

			        if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
			        {
				    return HTTPInternalServerError;
			        }

			        nUsed += (size_t) nWritten;
		            }
	                }

	                nWritten = snprintf (
		            PNPage + nUsed, sizeof PNPage - nUsed,
		            "</ul>"
		            "<p><a href=\"/browse\">Back to browse root</a></p>"
		            "<p><a href=\"/\">Back to home</a></p>"
		            "</body>"
		            "</html>");

	                if (nWritten < 0 || (size_t) nWritten >= sizeof PNPage - nUsed)
	                {
		            return HTTPInternalServerError;
	                }

	            pBody = PNPage;
                    }
                    else if (strncmp (pPath, "/browse/roms/", 13) == 0)
                    {  
                        if (!m_Config.m_bExposeRoms)
                        {
                            return HTTPNotFound;
                        }
                    
	                const char *pEncodedFileName = pPath + 13;
	                if (pEncodedFileName == 0 || pEncodedFileName[0] == '\0')
	                {
		            return HTTPNotFound;
	                }

	                char FileName[512];
	                if (!URLDecode (pEncodedFileName, FileName, sizeof FileName))
	                {
		            return HTTPNotFound;
	                }

	                if (HasSlash (FileName) || IsDotName (FileName))
	                {
		            return HTTPNotFound;
	                }

	                char FullPath[512];
	                int nPathWritten = snprintf (
		            FullPath, sizeof FullPath,
		            "SD:/roms/%s", FileName);

	                if (nPathWritten < 0 || (unsigned) nPathWritten >= sizeof FullPath)
	                {
		            return HTTPInternalServerError;
	                }

	                DIR *pTestDir = opendir (FullPath);
	                if (pTestDir != 0)
	                {
		            closedir (pTestDir);
		            return HTTPNotFound;
	                }

	                FILE *pInput = fopen (FullPath, "rb");
	                if (pInput == 0)
	                {
		            return HTTPNotFound;
	                }

	                char SizeText[64];
	                if (fseek (pInput, 0, SEEK_END) == 0)
	                {
		            long nFileSize = ftell (pInput);
		            if (nFileSize >= 0)
		            {
			        snprintf (SizeText, sizeof SizeText, "%ld bytes", nFileSize);
		            }
		            else
		            {
			        snprintf (SizeText, sizeof SizeText, "unknown");
		            }
	                }
	                else
	                {
		            snprintf (SizeText, sizeof SizeText, "unknown");
	                }

	                fclose (pInput);

	                int nWritten = snprintf (
		            PNPage, sizeof PNPage,
		            "<html>"
		            "<head><title>File detail: roms/%s</title></head>"
                            "<body>"
                            "<h1>File detail: roms/%s</h1>"
		            "<p>Read-only file detail page. No download or write operations are enabled.</p>"
		            "<ul>"
		            "<li>File name: %s</li>"
                            "<li>SD path: %s</li>"
                            "<li>File size: %s</li>"
		            "</ul>"
		            "<p><a href=\"/browse/roms\">Back to roms</a></p>"
		            "<p><a href=\"/browse\">Back to browse root</a></p>"
		            "<p><a href=\"/\">Back to home</a></p>"
		            "</body>"
		            "</html>",
                            FileName,
                            FileName,
                            FileName,
                            FullPath,
                            SizeText);

	                if (nWritten < 0 || (unsigned) nWritten >= sizeof PNPage)
	                {
		            return HTTPInternalServerError;
	                }

	                pBody = PNPage;
                    }
                    else
                    {
	                 return HTTPNotFound;
                    }

			unsigned nLength = strlen (pBody);
			if (*pLength < nLength)
			{
				return HTTPInternalServerError;
			}

			memcpy (pBuffer, pBody, nLength);
			*pLength = nLength;
			*ppContentType = "text/html; charset=iso-8859-1";

			return HTTPOK;
		}

	private:
		TNetFileServerConfig m_Config;
		u16 m_nPort;
	};
}

CNetFileServer::CNetFileServer (void)
:	m_bInitialized (false),
	m_bStartAttempted (false),
	m_bRunningLogged (false),
	m_bHTTPStarted (false),
	m_bTFTPStarted (false),
	m_bOwnNetSubSystem (false),
	m_pNetSubSystem (0),
	m_pHTTPDaemon (0),
	m_pTFTPServer (0)
{
}

CNetFileServer::~CNetFileServer (void)
{
	Shutdown ();
}

bool CNetFileServer::ParseIPv4 (const std::string& Text, u8 Address[4])
{
	unsigned a, b, c, d;
	char Tail = '\0';

	if (sscanf (Text.c_str (), "%u.%u.%u.%u%c", &a, &b, &c, &d, &Tail) != 4)
	{
		return false;
	}

	if (a > 255 || b > 255 || c > 255 || d > 255)
	{
		return false;
	}

	Address[0] = (u8) a;
	Address[1] = (u8) b;
	Address[2] = (u8) c;
	Address[3] = (u8) d;

	return true;
}

bool CNetFileServer::Initialize (const TNetFileServerConfig& Config)
{
	Shutdown ();

	m_Config = Config;
	m_bInitialized = true;
	m_bStartAttempted = false;
	m_bRunningLogged = false;
	m_bHTTPStarted = false;
	m_bTFTPStarted = false;
	m_bOwnNetSubSystem = false;
	m_pNetSubSystem = 0;
	m_pHTTPDaemon = 0;
	m_pTFTPServer = 0;

	DebugTX::WriteString("NETFILE armed\r\n");
	return true;
}

bool CNetFileServer::InitializeWithNetSubSystem (
	const TNetFileServerConfig& Config,
	CNetSubSystem *pNetSubSystem)
{
	Shutdown ();

	if (pNetSubSystem == 0)
	{
		DebugTX::WriteString("NETFILE external stack missing\r\n");
		return false;
	}

	m_Config = Config;
	m_bInitialized = true;
	m_bStartAttempted = true;
	m_bRunningLogged = false;
	m_bHTTPStarted = false;
	m_bTFTPStarted = false;
	m_bOwnNetSubSystem = false;
	m_pNetSubSystem = pNetSubSystem;
	m_pHTTPDaemon = 0;
	m_pTFTPServer = 0;

	DebugTX::WriteString("NETFILE armed (external stack)\r\n");
	return true;
}

void CNetFileServer::Shutdown (void)
{
	if (m_pTFTPServer != 0)
	{
		delete m_pTFTPServer;
		m_pTFTPServer = 0;
	}

	if (m_pHTTPDaemon != 0)
	{
		delete m_pHTTPDaemon;
		m_pHTTPDaemon = 0;
	}

	if (m_bOwnNetSubSystem && m_pNetSubSystem != 0)
	{
		delete m_pNetSubSystem;
	}

	m_pNetSubSystem = 0;
	m_bOwnNetSubSystem = false;
	m_bInitialized = false;
	m_bStartAttempted = false;
	m_bRunningLogged = false;
	m_bHTTPStarted = false;
	m_bTFTPStarted = false;
}

void CNetFileServer::Process (void)
{
	if (!m_bInitialized)
	{
		return;
	}

	if (!m_bStartAttempted)
	{
		m_bStartAttempted = true;

		if (m_Config.m_bDHCP)
		{
			m_pNetSubSystem = new CNetSubSystem (0, 0, 0, 0, m_Config.m_HostName.c_str ());
		}
		else
		{
			u8 IP[4];
			u8 Mask[4];
			u8 Gateway[4];

			if (   !ParseIPv4 (m_Config.m_IP, IP)
			    || !ParseIPv4 (m_Config.m_Mask, Mask)
			    || !ParseIPv4 (m_Config.m_Gateway, Gateway))
			{
				DebugTX::WriteString("NETFILE invalid static IPv4 config\r\n");
				return;
			}

			m_pNetSubSystem = new CNetSubSystem (IP, Mask, Gateway, 0, m_Config.m_HostName.c_str ());
		}

		if (m_pNetSubSystem == 0)
		{
			DebugTX::WriteString("NETFILE alloc failed\r\n");
			return;
		}

		m_bOwnNetSubSystem = true;

		DebugTX::WriteString("NETFILE stack starting\r\n");

		if (!m_pNetSubSystem->Initialize (FALSE))
		{
			DebugTX::WriteString("NETFILE stack init failed\r\n");
			return;
		}

		DebugTX::WriteString("NETFILE stack started\r\n");
	}

	if (m_pNetSubSystem == 0)
	{
		return;
	}

	if (m_bOwnNetSubSystem)
	{
		m_pNetSubSystem->Process ();
	}

	if (!m_bRunningLogged && m_pNetSubSystem->IsRunning ())
	{
		m_bRunningLogged = true;
		DebugTX::WriteString("NETFILE running\r\n");
	}

	if (!m_bHTTPStarted && m_pNetSubSystem->IsRunning ())
	{
		m_pHTTPDaemon = new CMiniJV880HTTPDaemon (
			m_pNetSubSystem,
			m_Config,
			(u16) m_Config.m_nPort);

		if (m_pHTTPDaemon != 0)
		{
			m_bHTTPStarted = true;
			DebugTX::WriteString("NETFILE http started\r\n");
		}
		else
		{
			DebugTX::WriteString("NETFILE http alloc failed\r\n");
		}
	}

	if (!m_bTFTPStarted && m_Config.m_bTFTPEnable && m_pNetSubSystem->IsRunning ())
	{
		m_pTFTPServer = new CMiniJV880TFTPDaemon (
			m_pNetSubSystem,
			m_Config.m_bWriteEnable ? TRUE : FALSE);

		if (m_pTFTPServer != 0)
		{
			m_bTFTPStarted = true;
			DebugTX::WriteString(
				m_Config.m_bWriteEnable
					? "NETFILE tftp started (rw stage "
					: "NETFILE tftp started (ro kernel");
			if (m_Config.m_bWriteEnable)
			{
				DebugTX::WriteString(kKernelStagePath);
			}
			DebugTX::WriteString(")\r\n");
		}
		else
		{
			DebugTX::WriteString("NETFILE tftp alloc failed\r\n");
		}
	}
}

bool CNetFileServer::IsInitialized (void) const
{
	return m_bInitialized;
}
