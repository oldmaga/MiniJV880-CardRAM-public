# Manuale Remote GUI / tastiera remota

MiniJV880 include una Remote GUI esterna lato PC:

    tools/minijv880_remote_gui.py

Questo strumento viene eseguito sul PC di sviluppo. Non è compilato nel firmware MiniJV880 e non viene servito dal server HTTP embedded del MiniJV880.

L'obiettivo è fornire un pannello remoto pratico per sviluppo, manutenzione, test e procedure documentate del JV-880, mantenendo piccolo e stabile il server HTTP embedded.

La Remote GUI è utile soprattutto quando:

- il MiniJV880 non è fisicamente vicino al PC;
- servono test ripetuti del pannello frontale durante lo sviluppo;
- le procedure del manuale originale JV-880 sono più facili da seguire con un pannello virtuale simile al JV-880;
- è utile vedere il readback di LCD, cursore e LED durante un test;
- servono gesture remote con tasti mantenuti premuti, per esempio DATA hold o PREVIEW hold.

## 1. Requisiti di sistema

La Remote GUI è uno strumento Python/Tkinter lato PC.

Sul PC servono:

- Python 3;
- supporto Tkinter per Python;
- accesso di rete al server HTTP del MiniJV880.

Per il readback live completo di LCD e LED è consigliato anche il supporto seriale:

- `pyserial`;
- un adattatore seriale collegato all'uscita seriale di debug del MiniJV880;
- permessi di accesso al dispositivo seriale, per esempio `/dev/ttyUSB0`.

Su sistemi Debian/Ubuntu i pacchetti tipici sono:

    sudo apt install python3 python3-tk python3-serial

Se `python3-serial` non è disponibile, oppure se si usa un ambiente virtuale Python, installare `pyserial` nell'ambiente Python usato per avviare la GUI.

Il monitor seriale è passivo: legge l'output seriale del MiniJV880 dal lato PC e non invia comandi via seriale.

Le impostazioni seriali predefinite sono:

- 38400 baud;
- 8 bit dati;
- nessuna parità;
- 1 bit di stop.

I comandi remoti vengono comunque inviati via HTTP.

## 2. Avvio della Remote GUI

Dalla root del repository:

    python3 tools/minijv880_remote_gui.py

Esempio:

    cd ~/MiniJV880-CardRAM-public
    python3 tools/minijv880_remote_gui.py

La GUI si apre come applicazione desktop Tkinter sul PC.

Se Tkinter manca, installare il pacchetto Tk per Python, per esempio:

    sudo apt install python3-tk

Se il monitor seriale non parte, verificare che `pyserial` sia installato e che l'utente corrente possa accedere al dispositivo seriale.

Su molti sistemi Linux può essere necessario aggiungere l'utente al gruppo `dialout`:

    sudo usermod -aG dialout "$USER"

Dopo aver cambiato i gruppi utente, uscire e rientrare nella sessione.

## 3. Connessione al MiniJV880

La Remote GUI comunica con MiniJV880 tramite piccoli endpoint HTTP.

URL predefinito:

    http://192.168.1.50:8080

L'URL può essere modificato nella GUI.

Usare lo strumento solo su una rete locale fidata. Non esporre il server HTTP del MiniJV880 a Internet.

La Remote GUI non è una grossa pagina web servita dal MiniJV880. È una applicazione PC che invia piccoli comandi HTTP al dispositivo.

## 4. Panoramica della finestra principale

L'area superiore contiene:

- connessione e stato HTTP;
- display LCD a due righe;
- readback del cursore;
- controlli del monitor seriale;
- refresh manuale LCD e fallback HTTP;
- eventuale log/dettagli seriali.

L'area inferiore contiene i controlli della tastiera remota / pannello frontale.

A seconda della versione, la GUI può fornire due viste complementari:

- vista hardware MiniJV880 attuale;
- vista orientata al pannello originale JV-880.

La vista hardware MiniJV880 attuale riflette il layout pratico dei pulsanti disponibili sul MiniJV880.

La vista orientata al JV-880 originale serve ad aiutare a seguire le procedure del manuale originale JV-880. È un pannello virtuale orientato al test, non una dichiarazione che il pannello fisico MiniJV880 sia una riproduzione perfetta uno-a-uno del JV-880 originale.

## 5. Modello dei pulsanti remoti

La Remote GUI usa il modello di controllo remoto HTTP piccolo già previsto dagli endpoint diagnostici firmware.

Le famiglie di comandi tipiche includono:

    /rraw?a=tap&m=
    /rraw?a=down&m=
    /rraw?a=up&m=
    /renc?d=cw
    /renc?d=ccw
    /rclr

Normalmente i pulsanti della GUI nascondono questi URL all'utente.

Concettualmente esistono tre azioni remote sui pulsanti:

- tap;
- down;
- up.

Un tap è una pressione breve completa generata dalla GUI.

Un down avvia uno stato remoto di pulsante mantenuto premuto.

Un up rilascia uno stato remoto di pulsante mantenuto premuto.

L'azione `Clear remote` è un reset di sicurezza che rilascia gli stati remoti mantenuti se lo stato tra GUI e MiniJV880 diventa incerto.

## 6. Uso base dei pulsanti

Per le normali pressioni brevi, usare le azioni tap/click.

Esempi tipici:

- UTILITY apre il menu Utility;
- SYSTEM apre il menu System;
- EDIT entra nei workflow di edit;
- PATCH/PERFORMANCE commuta tra aree Patch e Performance;
- LEFT e RIGHT muovono il cursore;
- ENTER conferma l'operazione selezionata;
- il comportamento di uscita dipende dal workflow del firmware originale e dal modo corrente.

La Remote GUI invia comandi al firmware MiniJV880. Il risultato effettivo è determinato dallo stato del firmware JV-880 emulato, esattamente come con i pulsanti fisici.

## 7. Uso dell'encoder

La Remote GUI può inviare comandi di rotazione encoder:

- senso orario;
- senso antiorario.

Questi comandi vengono inviati tramite il piccolo endpoint encoder:

    /renc?d=cw
    /renc?d=ccw

Usare i controlli encoder quando il workflow originale prevede movimento VALUE / DATA wheel.

Esempi:

- scorrimento delle patch;
- modifica di un valore selezionato;
- navigazione tra opzioni del menu Utility;
- cambio di destinazione Card/Internal quando il firmware si aspetta input encoder.

La GUI non modifica direttamente i parametri JV-880. Emula soltanto l'azione del pannello frontale.

## 8. Tasti mantenuti premuti e long press

Alcuni workflow MiniJV880 richiedono un tasto mantenuto premuto.

Esempi:

- PREVIEW hold;
- DATA hold per workflow nativi A/B/I/C o Card/C;
- ENTER down / comportamento simile a ENTER long mentre la pressione è ancora attiva;
- TONE SELECT hold, dove applicabile.

Per questi workflow usare i controlli down/up espliciti o i controlli hold dedicati presenti nella GUI.

Regola importante:

    rilasciare sempre un tasto remoto mantenuto premuto dopo il test.

Se lo stato diventa incerto, usare:

    Clear remote

Questo è particolarmente utile dopo un test interrotto, un ritardo di rete, la chiusura della GUI o un hold accidentale.

## 9. DATA hold e workflow Card/C

In MiniJV880-CardRAM, DATA ha un ruolo pratico speciale.

Sul MiniJV880 fisico:

- DATA short press viene usato per overlay/menu SR expansion;
- DATA long press viene usato per passare DATA al firmware originale dove serve la selezione nativa A/B/I/C o Card/C.

La Remote GUI aiuta a testare questi workflow perché può mantenere e rilasciare esplicitamente i pulsanti remoti.

Aree tipiche dove DATA hold può essere utile:

- selezione banco in Patch Play;
- selezione banco in Performance Play;
- selezione destinazione Internal/Card in Patch Write;
- selezione destinazione Internal/Card in Performance Write;
- selezione banco sorgente in Patch Copy;
- selezione banco sorgente in Performance Copy.

Il comportamento esatto dipende comunque dal firmware MiniJV880 corrente e dalla schermata JV-880 corrente.

## 10. Readback LCD

La GUI può leggere l'LCD remoto in due modi.

Percorso interattivo preferito:

- readback passivo seriale di LCD/cursore, quando il monitor seriale è attivo.

Percorso HTTP di fallback:

- `/rlcd.txt` all'avvio;
- `Refresh LCD` manuale;
- fallback event-driven dopo comandi remoti quando il monitor seriale non è attivo.

La GUI non usa polling HTTP periodico.

Quando il monitor seriale è attivo, il fallback HTTP automatico del LCD viene saltato.

Il cursore LCD viene letto dallo stato LCD del firmware, non dedotto dal contenuto testuale.

Il readback del cursore può arrivare da eventi seriali passivi come:

    LCDC|ROW=...|COL=...|ENABLED=...|VISIBLE=...|ADDR=...

oppure dalla riga `CURSOR:` restituita da `/rlcd.txt`.

Il firmware evita flooding seriale dovuto al solo blink del cursore: gli aggiornamenti seriali del cursore vengono emessi quando cambiano testo LCD o posizione del cursore, non per ogni fase di lampeggio.

## 11. Readback LED

Quando il monitor seriale parte, la GUI esegue una piccola lettura HTTP da:

    /rled.txt

Questo serve a sincronizzare lo stato LED iniziale.

Dopo questa sincronizzazione, gli aggiornamenti LED/LCD dovrebbero normalmente arrivare dagli eventi seriali passivi.

Il readback LED è utile quando si testano pulsanti mode, stato del pannello e workflow in cui lo stato LED visibile è importante.

## 12. Personalizzazione display

La finestra `Display...` fornisce personalizzazione lato PC del rendering LCD remoto.

Le impostazioni disponibili possono includere:

- preset colore LCD;
- tono dello sfondo;
- stile carattere;
- colore testo;
- opzione di inversione sfondo/testo.

Queste impostazioni influenzano solo il display della GUI PC.

Non modificano:

- il firmware MiniJV880;
- l'LCD fisico;
- i dati restituiti da `/rlcd.txt`;
- gli eventi seriali LCD.

Il renderer dot-matrix è pensato per somigliare di più al display LCD a caratteri.

I renderer testuali sono utili come alternative quando si preferisce una visualizzazione desktop più chiara.

## 13. Nota di sicurezza sui tasti mantenuti premuti

L'endpoint HTTP LCD `/rlcd.txt` è read-only, ma passa comunque dal normale percorso HTTP.

`Refresh LCD` manuale non dovrebbe essere usato mentre sono attive gesture remote hold, come:

- DATA hold;
- TONE SELECT hold;
- ENTER down / ENTER long mentre la pressione è ancora attiva;
- PREVIEW hold.

La GUI evita il fallback HTTP automatico del LCD durante gli hold locali, ma il refresh manuale resta una azione diagnostica manuale.

Se una gesture hold è attiva, rilasciarla prima di usare il refresh manuale.

Se lo stato della GUI sembra incoerente, premere:

    Clear remote

## 14. Monitor seriale

Il monitor seriale è un readback passivo lato PC dell'output seriale di debug MiniJV880.

È utile per:

- tracciamento live LCD;
- tracciamento cursore;
- tracciamento LED;
- verifica degli effetti dei comandi remoti;
- riduzione del traffico HTTP embedded durante i test interattivi.

Il monitor seriale non invia comandi via seriale.

I comandi remoti usano comunque HTTP.

L'output seriale di debug MiniJV880 corrente è normalmente disponibile sulla linea seriale di debug dedicata usata dal progetto. Controllare la documentazione hardware/front-panel per le note di cablaggio correnti.

## 15. Persistenza impostazioni

La GUI salva le proprie impostazioni lato PC alla chiusura.

Percorso predefinito:

    ~/.config/minijv880_remote_gui/settings.json

Se `XDG_CONFIG_HOME` è impostato, viene usato al posto di `~/.config`.

Le impostazioni salvate includono:

- URL MiniJV880;
- timeout HTTP;
- porta seriale;
- baud rate seriale;
- impostazione fallback HTTP;
- geometria finestra;
- stato Show details;
- stato Show safety notes;
- preset colore LCD;
- tono sfondo LCD;
- colore testo LCD;
- opzione invert sfondo/testo LCD;
- stile carattere LCD.

Per resettare le impostazioni della GUI, chiudere la GUI e rimuovere il file impostazioni:

    rm -f ~/.config/minijv880_remote_gui/settings.json

## 16. Esempi di workflow

### 16.1 Aprire Utility dal PC

1. Avviare la Remote GUI.
2. Verificare che l'URL MiniJV880 sia corretto.
3. Premere/tappare UTILITY nella tastiera remota.
4. Osservare il readback LCD.
5. Premere/tappare di nuovo UTILITY oppure usare il workflow di uscita appropriato del firmware.

### 16.2 Testare la navigazione encoder

1. Aprire una schermata in cui il firmware JV-880 si aspetta cambi valore.
2. Usare i controlli encoder orario/antiorario.
3. Osservare il readback LCD.
4. Se il monitor seriale è attivo, verificare che gli aggiornamenti arrivino senza refresh manuale.

### 16.3 Testare DATA hold per selezione Card/C

1. Navigare a un workflow in cui il firmware originale si aspetta DATA + encoder.
2. Avviare DATA hold dalla Remote GUI.
3. Ruotare l'encoder da remoto.
4. Confermare che banco/destinazione selezionati cambino come previsto.
5. Rilasciare DATA.
6. Usare Clear remote se lo stato diventa incerto.

### 16.4 Testare PREVIEW hold

1. Selezionare una patch o schermata in cui PREVIEW è significativo.
2. Avviare PREVIEW hold.
3. Verificare che avvenga il comportamento preview previsto.
4. Rilasciare PREVIEW.
5. Usare Clear remote se necessario.

## 17. Limitazioni note

La Remote GUI è un aiuto per sviluppo e manutenzione.

Non è un sostituto completo del pannello fisico.

La vista orientata al JV-880 originale serve ad aiutare a seguire workflow originali JV-880, ma hardware e mapping firmware del MiniJV880 sono adattamenti pratici.

La pagina remota HTML e la Remote GUI Python hanno punti di forza diversi:

- la pagina HTML è semplice e portabile, ma non può accedere al monitor seriale PC;
- la Remote GUI Python è migliore per i test interattivi perché può usare il readback passivo seriale LCD/LED.

La latenza di rete può influenzare l'interazione remota.

Un tasto remoto mantenuto premuto dovrebbe sempre essere rilasciato esplicitamente.

Se un comando sembra interrotto o lo stato è incerto, usare:

    Clear remote

## 18. Risoluzione problemi

### La GUI non contatta MiniJV880

Controllare:

- indirizzo IP MiniJV880;
- porta HTTP;
- connessione Ethernet;
- che il server HTTP MiniJV880 sia abilitato;
- che PC e MiniJV880 siano sulla stessa rete locale fidata.

### Tkinter manca

Installare il pacchetto Tkinter per il Python di sistema:

    sudo apt install python3-tk

### Il monitor seriale non parte

Controllare:

- percorso dispositivo seriale, per esempio `/dev/ttyUSB0`;
- baud rate;
- permessi utente per il dispositivo seriale;
- che `pyserial` sia installato;
- che nessun altro programma stia usando la porta seriale.

### Permission denied su `/dev/ttyUSB0`

Su molti sistemi Linux, aggiungere l'utente al gruppo `dialout`:

    sudo usermod -aG dialout "$USER"

Poi uscire e rientrare nella sessione.

### LCD non si aggiorna automaticamente

Avviare il monitor seriale e verificare che vengano ricevuti eventi seriali LCD MiniJV880.

Se il monitor seriale non è attivo, usare `Refresh LCD` manuale solo quando non è attiva nessuna gesture remota hold.

### Lo stato GUI sembra incoerente dopo una gesture hold

Premere:

    Clear remote

Poi ripetere il test da uno stato noto.

## 19. Documentazione correlata

Vedere anche:

- `docs/remote-gui.md`
- `docs/pc-side-tools.md`
- `docs/features-and-limitations.md`
- `docs/hardware-front-panel-notes.md`
- `docs/network-maintenance.md`
