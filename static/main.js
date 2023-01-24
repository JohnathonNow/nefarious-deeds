console.log("Hello world!")

let socket = null;

let pitch = document.getElementById("pitch");
let score1 = document.getElementById("score1");
let score2 = document.getElementById("score2");
let gametime = document.getElementById("gametime");
let events = {};
let ping, lag = 0;
let players = [];
let ball = null;
let x = 0, y = 0;
let cx = pitch.offsetLeft + 540;
let cy = pitch.offsetTop + 300;
let interval = null;
let score = null;
let base = null;
let delta = null;

function connect(callback) {
	socket = new WebSocket(((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/connect?id=" + id);
	socket.onmessage = function(message) {
		let decoded = JSON.parse(message.data);
		for (type in decoded) {
			let data = decoded[type];
			//console.log(type, data);
			events[type](data);
		}
	}
	socket.onopen = function() {
		ping = Date.now();
		socket.send(JSON.stringify({ping: null}));
		if (callback) callback();
	}
	return socket;
}

let id = sessionStorage.getItem("id");
if (id === null) {
	let arr = new Uint8Array(16);
	window.crypto.getRandomValues(arr);
	id = Array.from(arr).map(x => x.toString(16).padStart(2, "0")).join("");
	sessionStorage.setItem("id", id);
}

connect();

function send(event, data) {
	if (socket.readyState != 1) {
		connect(send.bind(null, ...arguments));
	} else {
		let message = {};
		message[event] = data;
		socket.send(JSON.stringify(message));
	}
}

window.send = send;

window.addEventListener("resize", function(event) {
	cx = pitch.offsetLeft + 540;
	cy = pitch.offsetTop + 300;
});

document.body.addEventListener("mousemove", function(event) {
	x = (event.pageX - cx) / 6;
	y = (event.pageY - cy) / 6;
});

document.body.addEventListener("mousedown", function(event) {
	let time = Date.now() / 1000 - delta;
	send("game/event", [time - lag, x, y, "Kick"]);
});

events["pong"] = function(data) {
	lag = (Date.now() - ping) / 2000;
	console.log("delay", lag);
}

events["game/create"] = function(data) {
}

events["game/list"] = function(data) {
};

events["game/join"] = function(data) {
};

function tick() {
	let time = Date.now() / 1000 - delta;
	let seconds = Math.floor(time);
	gametime.textContent = Math.floor(seconds / 60).toString() + ":" + (seconds % 60).toString().padStart(2, "0");
	send("game/event", [time, x, y, "Move"]);
	let dt = time - base;
	players.forEach(player => {
		let style = player.element.style;
		style.left = (player.x + dt * player.dx + 90) * 6 - 15 + "px";
		style.top = (player.y + dt * player.dy + 50) * 6 - 15 + "px";
		player.element.classList.remove("handler");
	});
	let style = ball.element.style;
	let handler = ball.handler;
	if (handler == -1) {
		style.display = null;
		style.left = (ball.x + dt * ball.dx + 90) * 6 - 6 + "px";
		style.top = (ball.y + dt * ball.dy + 50) * 6 - 6 + "px";
	} else {
		style.display = "none";
		if (handler >= 0) players[handler].element.classList.add("handler");
	}
}

events["game/start"] = function(data) {
	for (let element = pitch.firstChild; element; element = pitch.firstChild) {
		pitch.removeChild(element);
	}
	let index = data[0];
	players = data[1].map((info, i) => {
		let name = info[0];
		let team = info[1];
		let element = document.createElement("div");
		element.classList.add("player");
		element.classList.add("team" + team.toString());
		let label = document.createElement("span");
		if (i == index) {
			label.textContent = "☺";
		} else {
			label.textContent = i.toString();
		}
		element.appendChild(label);
		pitch.appendChild(element);
		return {name, team, element, x: 0, y: 0, dx: 0, dy: 0};
	});
	let element = document.createElement("div");
	element.classList.add("ball");
	ball = {element, x: 0, y: 0, dx: 0, dy: 0, handler: -2};
	pitch.appendChild(element);
	delta = Date.now() / 1000 - data[0];
	interval = setInterval(tick, 50);
};

events["game/state"] = function(data) {
	base = data[0];
	delta = Date.now() / 1000 - base;
	score = data[1];
	score1.textContent = score[0].toString();
	score2.textContent = score[1].toString();
	let ballData = data[2];
	if (ballData === null) {
		ball.handler = -2;
	} else if (typeof(ballData) === "number") {
		ball.handler = ballData - 1;
	} else {
		ball.handler = -1;
		ball.x = ballData[0];
		ball.y = ballData[1];
		ball.dx = ballData[2];
		ball.dy = ballData[3];
	}
	players.forEach((player, i) => {
		let playerData = data[i + 3];
		player.x = playerData[0];
		player.y = playerData[1];
		player.dx = playerData[2];
		player.dy = playerData[3];
	});
};
