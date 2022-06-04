#include "index.h"

const char *index_page = R"!(<html>
	<head>
		<meta http-equiv="content-type" content="text/html; charset=utf-8" />
		<title>ESP32 Camera</title>
        <script>
            function snap()
            {
                window.stop();
                if (imagepanel.innerHTML != '')
                {   
                    image.src="/still";
                }
                else
                {
                    imagepanel.innerHTML='<img id="image" class="center" src="/still"/>';
                }
            }
            function stream()
            {
                window.stop();
                if (imagepanel.innerHTML != '')
                {   
                    image.src="/stream";
                }
                else
                {
                    imagepanel.innerHTML='<img id="image" class="center" src="/stream"/>';
                }
            }
            function restart()
            {
                window.stop();
                imagepanel.innerHTML='Restarting';
                window.location.href = '/restart';
            }
            function vflip()
            {
                window.stop();
                var xhr = new XMLHttpRequest();
                xhr.open("GET", "/config?vflip=2");
                xhr.send();
                if (imagepanel.innerHTML != '')
                {   
                    image.src=image.src;
                }
            }
            function hflip()
            {
                window.stop();
                var xhr = new XMLHttpRequest();
                xhr.open("GET", "/config?hflip=2");
                xhr.send();
                if (imagepanel.innerHTML != '')
                {   
                    image.src=image.src;
                }
            }
        </script>
        <style>
        #imagepanel {
            display: grid;
            height: 100%;
        }
        .center {
            max-width: 100%;
            max-height: 100vh;
            margin: auto;
        }
        </style>
	</head>
	<body>
		<h1>ESP32 Camera</h1>
        <ul>
            <li><a href="stream">Stream</a></li>
            <li><button onclick="snap()">Still</button></li>
            <li><button onclick="stream()">Stream</button></li>
            <li><button onclick="vflip()">VFlip</button></li>
            <li><button onclick="hflip()">HFlip</button></li>
            <li><a href="/status">Status</a></li>
            <li><a href="/update">Firmware update</a></li>
            <li><button onclick="restart()">Restart</button></li>
        </ul>
        <div id="imagepanel"></div>
	</body>
</html>)!";
