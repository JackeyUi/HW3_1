# HW3_1
1. Download the files into a clean dir.
# Before compile the files, you should check the ip address ,SSID and the password in the code.
# Gestrue UI
2. Use screen to check if the mbed is connected to wifi.
3. Exercute the mqtt_client.py in wifi_mqtt
4. do /doGUI/run 1 to start the gestrue UI mode. While in this mode, LED1 is on.
5. you can use 3 gestrues to select different angle (1) "ring" is for 30 degree (2) "slope" is for 45 degree (3) "x-axis"(move horizontally) is for 60 degree
(you can check the current selection through screen or uLCD.)
6. Press user button to send the selection to python (by wifi), after python recevied, it would send a command ("/doGUI/run 0") to the mbed to stop the gestrue UI.
Now the mbed is back in RPC loop
# Angle detection
7. do /doANG/run 1 to start the angle detection mode. While in this mode, LED2 is on.
8. right after the command, the mbed will initialization process for 5 seconds, during the process the LED3 is on, you should place the mbed on table.
9. The uLCD would start displaying the angle of mbed, if the detected angle is larger than the previous selection in gestrue UI, mbed would send the angle to python(by wifi)
10. After python receive 10 angle values, it would send a command ("/doANG/run 0") to the mbed to stop the angle detection.
Now the mbed is in RPC loop
# the demo video
https://youtu.be/fvcyu7RrL28
