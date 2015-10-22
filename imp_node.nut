//
// Electric Imp main loop -- bootloaded from the cloud. Forwards data to the back-end server
// Imp Code - Squirrel


local haveNewData=0;
atmel <- hardware.uart57;
function initUart()
{
    hardware.configure(UART_57);    // Using UART on pins 5 and 7
    // 9600 baud works well, no parity, 1 stop bit, 8 data bits.
    // Provide a callback function, serialRead, to be called when data comes in:
    atmel.configure(9600, 8, PARITY_NONE, 1, NO_CTSRTS, serialRead);
}

// serialRead() will be called whenever serial data is passed to the imp. It
//  will read the data in, and send it out to the agent.
function serialRead()
{
    server.log("Imp reading data");
    local c = atmel.read(); // Read serial char into variable c
    local t = atmel.read();
    
    local dataString = c.tostring() + t.tostring();
    server.log(dataString);
    
    agent.send("impSerialIn", dataString);
}

function sendCommand(command) {
    atmel.write(command.tostring());
    server.log("Imp sending data:");
    server.log(command.tostring());
}

// agent.on("dataToSerial") will be called whenever the agent passes data labeled
//  "dataToSerial" over to the device. This data should be sent out the serial
//  port, to the Arduino.
agent.on("dataToSerial", function(c)
{
    arduino.write(c.tostring()); // Write the data out the serial port.
});



// Setup //
server.log("Serial Pipeline Open!"); // A warm greeting to indicate we've begun
initUart(); // Initialize the LEDs

//send command to uart
agent.on("command", sendCommand);

///EOF


