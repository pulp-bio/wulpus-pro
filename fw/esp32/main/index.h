#ifndef _INDEX_H
#define _INDEX_H

const char mainPage[] = R"=====(
<HTML>
    <HEAD>
        <TITLE>ESP32 Website</TITLE>

        <STYLE>
            h1 {
                color: #21130d;
                font-family: Helvetica, sans-serif;
                font-size: 48px;
                font-weight: bold;
                text-align: center;
            }
            body {
                background-color: #eeeee4;
                font-family: Helvetica, sans-serif;
                font-size: 24px;
                text-align: center;
            }
        </STYLE>
    </HEAD>
    <BODY>
        <CENTER>
            <BR>
            <H1>Hello, world!</H1>
            My IP address is: <b>%IP%</b>
        </CENTER>
    </BODY>
</HTML>
)=====";

#endif