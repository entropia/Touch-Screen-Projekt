Hirose-Stecker:
https://www.hirose.com/de/product/document?clcode=CL0580-2412-8-60&productname=FH26W-39S-0.3SHW(60)&series=FH26&documenttype=Catalog&lang=de&documentid=D49355_en

https://www.mouser.de/ProductDetail/Hirose-Connector/FH26W-39S-0.3SHW60?qs=vcbW%252B4%252BSTIppYpLJPh73gg%3D%3D

J342

Level Shifter 3,3V => 2,8 V (display) & 1,8 V(touch )
3,7 V Raile direkt von Akku
3,3 V Raile

das Display kann auch mit 2 statt 4 DSI Lanes betreiben, 
das spart schonmal 4 differenzielle Leiter, 
aber dann müsst ihr die init-sequence im Treiber verändern.


Treiber panel-TSD-BV055HDE.c 
Display/DriverIC Datasheet 


Touch-Driver edt-ft5x06.c; 
ist aber zu 99% die Linux Mainline Kernel Version. 
Die einzige Änderung ist, dass der Touch-Treiber in seiner "probe()"-Funktion erst prüft, 
ob der Display-Treiber geladen wurde. Denn sonst hat der Touch-IC keinen Strom und wird nicht gefunden.
