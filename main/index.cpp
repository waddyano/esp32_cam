#include "camera.h"
#include "index.h"

static const char index_page_head[] = R"!(<html>
	<head>
		<meta http-equiv="content-type" content="text/html; charset=utf-8" />
        <link rel="icon" href="favicon.ico" type="image/x-icon" />
        <!-- <meta name="viewport" content="width=device-width, initial-scale=1.0"/> -->
		<title>%s</title>
        <script>)!";

const char *get_index_page_head()
{
    static char tmp[sizeof(index_page_head) + sizeof(camera_name)];
    const char *n = camera_name;
    if (n[0] == '\0')
    {
        n = "ESP32 Camera";
    }
    snprintf(tmp, sizeof(tmp), index_page_head, n);
    return tmp;
}

const char *index_page_body = R"!(
var eventSource = null;

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

function rel_url(u)
{
    let h = window.location.href;
    if (h.length > 0 && h[h.length - 1] != '/')
    {
        h = h + '/';
    }
    return h + u;
}

function loaded()
{
    let link = document.querySelector("link[rel~='icon']");
    link.href = rel_url('favicon.ico');
    status_button.href = rel_url('status');
    update_button.href = rel_url('update');
    pagehead.innerHTML = document.title;
    snap();
    fetch(rel_url('name')).then((response) => { return response.text(); }).then((text) => { cameraName.value = text; });
}
function snap()
{
    if (eventSource != null)
    {
        eventSource.close();
        eventSource = null;
    }
    if (imagepanel.innerHTML == '')
    {
        imagepanel.innerHTML='<img id="image" class="center"/>';
    }
    let img = document.querySelector('#image');
    fetch(rel_url('still')).then(function(response) 
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
        image.src=rel_url("stream");
    }
    else
    {
        imagepanel.innerHTML='<img id="image" class="center" src="' + rel_url('stream') + '>';
    }
    if (eventSource != null)
    {
        eventSource.close();
    }
    eventSource = new EventSource(rel_url('events'));
    eventSource.addEventListener("status", function(m) {
        let d = document.querySelector('#date');
        let l = '';
        try 
        {
            let ev = JSON.parse(m.data);
            if ('time' in ev)
            {
                l = l + ev.time;
            }
            if ('frames' in ev)
            {
                if (l.length > 0)
                {
                    l = l + ' ';
                }

                l = l + ev.frames + ' fps';
            }
        }
        catch(e) 
        {
        }
        d.innerHTML = l;
        //console.log(m);
    })
}
function restart()
{
    imagepanel.innerHTML='Restarting';
    window.location.href = rel_url('restart');
}
function setname()
{
    fetch(rel_url('config') + '?name=' + cameraName.value).
        then((resp) =>
            {
                fetch(rel_url('name')).then((r) => { return r.text(); }).then((text) => { cameraName.value = text; document.title = text; pagehead.innerHTML = text; });
            });
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
    xhr.open("GET", rel_url('config') + "?vflip=2");
    xhr.send();
    refresh();
}
function hflip()
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", rel_url('config') + "?hflip=2");
    xhr.send();
    refresh();
}
function framesize(size)
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", rel_url('config') + "?resolution=" + size);
    xhr.send();
    refresh();
}
function bright(level) 
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", rel_url('config') + "?brightness=" + level);
    xhr.send();
    refresh();
}
function contrast(level) 
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", rel_url('config') + "?contrast=" + level);
    xhr.send();
    refresh();
}
function led() 
{
    var xhr = new XMLHttpRequest();
    xhr.open("GET", rel_url('led'));
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
#imagepanel { display: grid; height: auto; }
h1 { text-align: center; }
.tabcenter { margin-left: auto; margin-right: auto; }
.center { max-width: 100%; max-height: 100vh; margin: auto; }
button { border: 0; border-radius: 0.3rem; background:#1fa3ec; color:#ffffff; line-height:2.4rem; font-size:1.2rem; width:180px;
-webkit-transition-duration:0.4s;transition-duration:0.4s;cursor:pointer;}
#cameraName { line-height:2.4rem; font-size:1.2rem; width:180px; }
button:hover{background:#0b73aa;}
.cb { border: 0; border-radius: 0.3rem; font-family: arial, sans-serif; color: black; line-height:2.4rem; font-size:1.2rem;}
input[type=checkbox] { height:1.2rem; width: 1.2rem;}
label.sl { vertical-align: bottom; }
#admin { display: none; }
#picture { display: none; }
#size { display: none; }
        </style>
	</head>
	<body onload="loaded()">
		<h1 id="pagehead">ESP32 Camera</h1>
        <table class="tabcenter"><tr>
            <td><button onclick="snap()">Still</button></td>
            <td><button onclick="stream()">Stream</button></td>
            <td><button onclick="vflip()">VFlip</button></td>
            <td><button onclick="hflip()">HFlip</button></td>
            <td><button onclick="led()">LED</button></td>
        </tr><tr>
            <td><input type="checkbox" onclick="toggle('#picture')"><span class="cb">Picture</span></td>
            <td><input type="checkbox" onclick="toggle('#size')"><span class="cb">Size</span></td>
            <td><input type="checkbox" onclick="toggle('#admin')"><span class="cb">Admin</span></td>
        </tr><tr id="picture">
            <td><label class="sl"><input type="range" min="-2" max="2" value="0" class="slider" id="bright" oninput="bright(this.value)">Brightness</label></td>
            <td><input type="range" min="-2" max="2" value="0" class="slider" id="contrast" oninput="contrast(this.value)"><label class="sl">Contrast</label></td>
        </tr><tr id="admin">
            <td><a id="status_button"><button type="button">Status</button></a></td>
            <td><a id="update_button"><button type="button">Update</button></a></td>
            <td><button onclick="restart()">Restart</button></td>
            <td><button onclick="setname()">Set Name:</button></td>
            <td><input type="text" id="cameraName" name="cameraName"></td>
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
