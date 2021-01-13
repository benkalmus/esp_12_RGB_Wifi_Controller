var rainbowEnable = false;
var pulseEnable = false;
var websocketServerLocation = 'ws://'+location.hostname+':81/';
var connection = new WebSocket(websocketServerLocation, ['arduino']);
connection.onopen = function () {
    connection.send('Connect ' + new Date());
    checkStatus();
};
connection.onerror = function (error) {
    console.log('WebSocket Error ', error);
    setTimeout(function(){
        connection = new WebSocket(websocketServerLocation);
        checkStatus();
    }, 1000);   //attempt reconnect
};
connection.onmessage = function (e) {  
    console.log('Server: ', e.data);
};
connection.onclose = function(){
    console.log('WebSocket connection closed');
    setTimeout(function(){
        connection = new WebSocket(websocketServerLocation);
        checkStatus();
    }, 1000);   //attempt reconnect
};

setInterval(function(){
    if (!checkStatus())
    {
        connection = new WebSocket(websocketServerLocation);
    }
}, 2500);

function sendRGB() {
    var r = document.getElementById('r').value**2/1023;
    var g = document.getElementById('g').value**2/1023;
    var b = document.getElementById('b').value**2/1023;
    
    var rgb = r << 20 | g << 10 | b;
    var rgbstr = '#'+ rgb.toString(16);    
    console.log('RGB: ' + rgbstr); 
    connection.send(rgbstr);
}

function rainbowEffect(){
    rainbowEnable = ! rainbowEnable;
    if(rainbowEnable){
        connection.send("R\n");
        console.log('Rainbow Effect Enabled');
        pulseEnable = false;
        document.getElementById('rainbow').style.backgroundColor = '#00878F';
        document.getElementById('r').className = 'disabled';
        document.getElementById('g').className = 'disabled';
        document.getElementById('b').className = 'disabled';
        document.getElementById('r').disabled = true;
        document.getElementById('g').disabled = true;
        document.getElementById('b').disabled = true;
        document.getElementById('button_pulse').disabled = true;
    } else {
        connection.send("N\n");
        console.log('Rainbow Effect Disabled');
        document.getElementById('rainbow').style.backgroundColor = '#999';
        document.getElementById('r').className = 'enabled';
        document.getElementById('g').className = 'enabled';
        document.getElementById('b').className = 'enabled';
        document.getElementById('r').disabled = false;
        document.getElementById('g').disabled = false;
        document.getElementById('b').disabled = false;
        document.getElementById('button_pulse').disabled = false;
        sendRGB();
    }  
}

function pulseEffect()
{
	pulseEnable = !pulseEnable;
    if(pulseEnable){
        rainbowEnable = false;
        connection.send("P\n");
        console.log('Pulse Effect Enabled');
        document.getElementById('button_pulse').style.backgroundColor = '#00878F';
        document.getElementById('rainbow').className = 'disabled';
        document.getElementById('rainbow').disabled = true;
    } else {
        connection.send("N\n");
        console.log('Pulse Effect Disabled');
        document.getElementById('button_pulse').style.backgroundColor = '#999';
        document.getElementById('rainbow').className = 'enabled';
        document.getElementById('rainbow').disabled = false;
        sendRGB();
    }  
}

function checkStatus()
{
    var indicator = document.getElementById("indicator");
    if(connection.readyState === connection.OPEN){	//open
        indicator.style.background='#42f48c';
        return true;
    }
    else if (connection.readyState ===connection.CONNECTING)		//amber, connecting
    {
        indicator.style.background='#fa0';
        return true;
    }
    else
    {
        indicator.style.background='#f44d41';		//red, no connection/err
        return false;
    }
}