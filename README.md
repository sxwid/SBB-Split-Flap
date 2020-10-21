# SBB-Split-Flap
ESP8266 Programm für die SBB Fallblattanzeiger. 
Die Ansteuerung basiert auf https://github.com/eni23/sbb-fallblatt.
Beinhaltet folgende Komponenten:

  - RTC DS3231, welche die Stunden/Minutenanzeige regelt, synchronisiert via NTP / WiFi
  - Luxsensor, welcher unter einer definierbaren Schwelle die Flaps auf Null stellt und nicht mehr betätigt (damit es nachts nicht ständig rattert
  - Temperatursensor, welcher die Umgebungstemperatur auf der Verspätungsanzeige ausgibt (angezeige Minuten plus 10 ) in Grad C
  - Poti, mit dem die Intervalle der neuen Stationen (zufällig generiert mit einer "Blacklist" von leeren Flaps)
  
Kommentare sind erwünscht.
