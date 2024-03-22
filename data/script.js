var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
//Init websocket when the page loads
window.addEventListener("load", onload);

function onload(event) {
  initWebSocket();
}

function getReadings() {
  websocket.send("websocket connection");
}

function initWebSocket() {
  console.log("Trying to open a WebSocket connection...");
  websocket = new WebSocket(gateway);
  websocket.onopen = onOpen;
  websocket.onclose = onClose;
  websocket.onmessage = onMessage;
}

function onOpen(event) {
  console.log("Connection opened");
}

function onClose(event) {
  console.log("Connection closed");
  setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
  console.log(event.data);
  var myObj = JSON.parse(event.data);
  var keys = Object.keys(myObj);

  for (var i = 0; i < keys.length; i++) {
    var key = keys[i];
    document.getElementById(key).innerHTML = myObj[key];
    if (key === "BOOT0") {
      console.log(`Boot0 : {myObj[key]}`);
      document.getElementById(key).dataset.status = myObj[key];
    }
  }
}

function testGet(event) {
  let id = document.getElementById("idInput").value;
  console.log(id);
  websocket.send(JSON.stringify({
    id: id,
  }));
}

// // Function to update the status on the page
// // Event listener for changes in the readyState
// const intervalID = setInterval(updateStatus, 5000);

// function updateStatus() {
//   const statusElement = document.getElementById('status');
//   console.log(`before ${statusElement.textContent}`);
//   if (websocket != undefined) {
//     websocket.send("test");
//     statusElement.textContent = `WebSocket readyState: ${websocket.readyState}`;
//   }
//   console.log(`after ${statusElement.textContent}`);
// }
