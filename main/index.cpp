#include "index.h"

const char *index_page = R"!(<html>
	<head>
		<meta http-equiv="content-type" content="text/html; charset=utf-8" />
        <!-- <meta name="viewport" content="width=device-width, initial-scale=1.0"/> -->
		<title>ESP32 Camera</title>
        <script>
function getString(dv, offset, length){
    let end = offset + length;
    let text = '';

    while (offset < end)
    {
        let val = dv.getUint8(offset++);
        if (val == 0) break;
        text += String.fromCharCode(val);
    }

    return text;
};

function parseIFD(dv, little, base, offset, callback) {
    var tagCount = dv.getUint16(offset, little);
    offset += 2;
    for (var i = 0; i < tagCount; i++)
    {
        let tag = dv.getUint16(offset + (i * 12), little);
        let type= dv.getUint16(offset + (i * 12) + 2, little);
        let count = dv.getUint32(offset + (i * 12) + 4, little);
        let dataOffset = dv.getUint32(offset + (i * 12) + 8, little);

        if (tag == 0x8769) 
        {
            let found = parseIFD(dv, little, base, base + dataOffset, callback);
            if (found)
            {
                return found;
            }
        }
        else if (tag == 0x9003 || tag == 0x132)
        {
            callback(getString(dv, base + dataOffset, 20));
            return true
        }
    }
    return false
}

function getDate(buffer, callback) {
    var dv = new DataView(buffer);
    if (dv.getUint16(0, false) != 0xFFD8)
    {
        return;
    }
    let length = dv.byteLength;
    let offset = 2;
    while (offset < length) 
    {
        if (dv.getUint16(offset+2, false) <= 8) return callback(-1);
        var marker = dv.getUint16(offset, false);
        offset += 2;
        if (marker == 0xFFE1) 
        {
            if (dv.getUint32(offset += 2, false) != 0x45786966) 
            {
                return;
            }

            var little = dv.getUint16(offset += 6, false) == 0x4949;
            let base = offset;
            offset += dv.getUint32(offset + 4, little);
            if (parseIFD(dv, little, base, offset, callback))
            {
                return;
            }
        }
        else if ((marker & 0xFF00) != 0xFF00)
        {
            break;
        }
        else
        { 
            offset += dv.getUint16(offset, false);
        }
    }
};

function snap()
{
    if (imagepanel.innerHTML == '')
    {
        imagepanel.innerHTML='<img id="image" class="center"/>';
    }
    let img = document.querySelector('#image');
    fetch('/still').then(function(response) 
    {
        return response.blob();
    }).then(function(blob) 
    {
        img.src = URL.createObjectURL(blob);
        let d = document.querySelector('#date');
        d.innerHTML = '';
        blob.arrayBuffer().then(function (buf)
        {
            getDate(buf, function(date) {
                d.innerHTML = date;
            });
        });
    });
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
function refresh()
{
    if (imagepanel.innerHTML != '' && image.src.startsWith("blob:"))
    {   
        snap();
    }
}
function vflip()
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/config?vflip=2");
    xhr.send();
    refresh();
}
function hflip()
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/config?hflip=2");
    xhr.send();
    refresh();
}
function framesize(size)
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/config?resolution=" + size);
    xhr.send();
    refresh();
}
function bright(level) 
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/config?brightness=" + level);
    xhr.send();
    refresh();
}
function contrast(level) 
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/config?contrast=" + level);
    xhr.send();
    refresh();
}
function toggle(id)
{
    let row = document.querySelector(id);
    if (row.style.display == "none" || row.style.display == "")
        row.style.display = "table-row";
    else
        row.style.display = "none";
}
        </script>
        <style>
body, textarea, button {font-family: arial, sans-serif;}
#date { line-height: 2.4rem; font-size:1.2rem; font-family: Sans-Serif; margin: 0 auto; }
#datewrapper { text-align:center; }
#imagepanel { display: block; height: auto; }
.center { max-width: 100%; max-height: 100vh; margin: auto; }
button { border: 0; border-radius: 0.3rem; background:#1fa3ec; color:#ffffff; line-height:2.4rem; font-size:1.2rem; width:180px;
-webkit-transition-duration:0.4s;transition-duration:0.4s;cursor:pointer;}
button:hover{background:#0b73aa;}
.cb { border: 0; border-radius: 0.3rem; font-family: arial, sans-serif; color: black; line-height:2.4rem; font-size:1.2rem;}
input[type=checkbox] { height:1.2rem; width: 1.2rem;}
label.sl { vertical-align: bottom; }
#admin { display: none; }
#picture { display: none; }
#size { display: none; }
        </style>
	</head>
	<body onload="snap()">
		<h1>ESP32 Camera</h1>
        <table><tr>
            <td><button onclick="snap()">Still</button></td>
            <td><button onclick="stream()">Stream</button></td>
            <td><button onclick="vflip()">VFlip</button></td>
            <td><button onclick="hflip()">HFlip</button></td>
        </tr><tr>
            <td><input type="checkbox" onclick="toggle('#picture')"><span class="cb">Picture</span></td>
            <td><input type="checkbox" onclick="toggle('#size')"><span class="cb">Size</span></td>
            <td><input type="checkbox" onclick="toggle('#admin')"><span class="cb">Admin</span></td>
        </tr><tr id="picture">
            <td><label class="sl"><input type="range" min="-2" max="2" value="0" class="slider" id="bright" oninput="bright(this.value)">Brightness</label></td>
            <td><input type="range" min="-2" max="2" value="0" class="slider" id="contrast" oninput="contrast(this.value)"><label class="sl">Contrast</label></td>
        </tr><tr id="admin">
            <td><a href="/status"><button type="button">Status</button></a></td>
            <td><a href="/update"><button type="button">Update</button></a></td>
            <td><button onclick="restart()">Restart</button></td>
        </tr><tr id="size">
            <td><button onclick="framesize('cif')">CIF</button></td>
            <td><button onclick="framesize('vga')">VGA</button></td>
            <td><button onclick="framesize('svga')">SVGA</button></td>
            <td><button onclick="framesize('xga')">XGA</button></td>
        </tr></table>
        <div id="datewrapper">
            <div id="date"></div>
        </div>
        <div id="imagepanel"></div>
	</body>
</html>)!";
