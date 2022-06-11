#include "index.h"

const char *index_page = R"!(<html>
	<head>
		<meta http-equiv="content-type" content="text/html; charset=utf-8" />
		<title>ESP32 Camera</title>
        <script>
            function snap()
            {
                if (imagepanel.innerHTML != '')
                {   
                    image.src="/still?x=" + new Date().getTime();
                }
                else
                {
                    imagepanel.innerHTML='<img id="image" class="center" src="/still"/>';
                }
            }
            function stream()
            {
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
                imagepanel.innerHTML='Restarting';
                window.location.href = '/restart';
            }
            function vflip()
            {
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
        #imagepanel { display: grid; height: 100%; }
        .center { max-width: 100%; max-height: 100vh; margin: auto; }
        button { border: 0; border-radius: 0.3rem; background:#1fa3ec; color:#ffffff; line-height:2.4rem; font-size:1.2rem; width:160px;
        -webkit-transition-duration:0.4s;transition-duration:0.4s;cursor:pointer;}
        button:hover{background:#0b73aa;}
        </style>
	</head>
	<body>
		<h1>ESP32 Camera</h1>
        <table><tr>
            <td><button onclick="snap()">Still</button></td>
            <td><button onclick="stream()">Stream</button></td>
            <td><button onclick="vflip()">VFlip</button></td>
            <td><button onclick="hflip()">HFlip</button></td>
            <td><a href="/status"><button type="button">Status</button></a></td>
            <td><a href="/update"><button type="button">Update</button></a></td>
            <td><button onclick="restart()">Restart</button></td>
        </tr></table>
        <div id="imagepanel"></div>
	</body>
</html>)!";
