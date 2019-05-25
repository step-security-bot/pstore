(function () {
    'use strict';

    var uptime = new WebSocket ("ws://" + window.location.host + "/uptime");
    uptime.onerror = function (error) {
        console.error(error);
    };
    uptime.onclose = function () {
        console.log("uptime websocket closed.");
    };
    uptime.onopen = function () {
        console.log("uptime websocket open.");
    };
    uptime.onmessage = function (msg) {
        var obj = JSON.parse(msg.data);
        var el = document.getElementById("message");
        if (el !== null) {
            el.textContent = obj.uptime !== undefined ? obj.uptime : 'Unknown';
        }
    };

    window.onload = () => {
        var request = new XMLHttpRequest ();
        request.open('GET', 'http://' + window.location.host + '/cmd/version');
        request.responseType = 'json';
        request.onload = () => {
            var el = document.getElementById('version');
            if (el !== null) {
                el.textContent = request.response.version;
            }
        };
        request.send();
    };

} ());
