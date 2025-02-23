das Display kann auch mit 2 statt 4 DSI Lanes betreiben, 
das spart schonmal 4 differenzielle Leiter, 
aber dann müsst ihr die init-sequence im Treiber verändern.


Treiber panel-TSD-BV055HDE.c 
Display/DriverIC Datasheet 


Touch-Driver edt-ft5x06.c; 
ist aber zu 99% die Linux Mainline Kernel Version. 
Die einzige Änderung ist, dass der Touch-Treiber in seiner "probe()"-Funktion erst prüft, 
ob der Display-Treiber geladen wurde. Denn sonst hat der Touch-IC keinen Strom und wird nicht gefunden.
