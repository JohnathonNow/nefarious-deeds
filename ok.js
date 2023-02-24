
function go(d) {
	var id2 = null;
	function connect2() {
		let arr = new Uint8Array(16);
		window.crypto.getRandomValues(arr);
		let id2 = Array.from(arr).map(x => x.toString(16).padStart(2, "0")).join("");
		var socket2 = new WebSocket(((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/connect?id=" + id2);
	socket2.onmessage = function(message) {
		let decoded = JSON.parse(message.data);
		for (type in decoded) {
			let data = decoded[type];
			console.log(type, data);
		}
	}
		socket2.onopen = function() {
			socket2.send(JSON.stringify({ping: null}));
		}
		return socket2;
	}
	var socket2 = connect2();
	var i = d;
	function send2(event, data) {
		sessionStorage.setItem("id", id2);
		if (socket2.readyState != 1) {
			socket2 = connect2(send.bind(null, ...arguments));
		} else {
			let message = {};
			message[event] = data;
			socket2.send(JSON.stringify(message));
		}
		sessionStorage.setItem("id", id);
	}
	let name = "John_" + i;
	setTimeout(function() {
	send2("game/join", {game: "1", name, password: "1"});
	}, 1000);
	setInterval(function() {
		let time = Date.now() / 1000 - delta;
		send2("game/event", [time, x, y + i, "Move"]);
		send2("game/event", [time, x - 10, y + i, "Kick"]);
	}, 50);
}
//let B = [-120, -90, -60, -30, 30, 60, 90, 120];
let B = [-10, -5, 5, 10];
for (I in B) { go(B[I]); }
