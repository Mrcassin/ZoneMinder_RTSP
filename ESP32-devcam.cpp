#include <Arduino.h>
#include <string.h>
#include "OV2640.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <AutoWifi.h>
#include <WiFiMulti.h>

WiFiMulti wifiMulti;

#include "SimStreamer.h"
#include "OV2640Streamer.h"
#include "OV2640.h"
#include "CRtspSession.h"

const char *ssid = "AC/DC";
const char *password = "RoJiVeMa144";

#define USEBOARD_AITHINKER

OV2640 cam;

WebServer server(80);
WiFiServer rtspServer(8554);

int err;
String streamchar = "";

IPAddress ZM(192,168,2,112);
WiFiClient telClient;
bool trStatus = false;

static void IRAM_ATTR detectsMovement(void *arg)
{
    Serial.println("MOTION DETECTED!!!!");
    //streamchar = "1|on+20|1|External Motion|External Motion";
    trStatus = true;
}


void handle_jpg_stream(void)
{
    WiFiClient client = server.client();
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    server.sendContent(response);

    while (1)
    {
        cam.run();
        if (!client.connected())
            break;
        response = "--frame\r\n";
        response += "Content-Type: image/jpeg\r\n\r\n";
        server.sendContent(response);

        client.write((char *)cam.getfb(), cam.getSize());
        server.sendContent("\r\n");
        if (!client.connected())
            break;
    }
}

void handle_alert_stream(void)
{

    WiFiClient client = server.client();
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    server.sendContent(response);

    while (1)
    {
        String lStream = streamchar;
        streamchar = "";
        int delkaStreamu= lStream.length();
        
        if (!client.connected())
            break;
        client.write(lStream.c_str(), delkaStreamu);
        
        if (!client.connected())
            break;
    }
}

void handle_jpg(void)
{
    WiFiClient client = server.client();
    cam.run();
    if (!client.connected())
    {
        return;
    }
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-disposition: inline; filename=capture.jpg\r\n";
    response += "Content-type: image/jpeg\r\n\r\n";
    server.sendContent(response);
    client.write((char *)cam.getfb(), cam.getSize());
}

void handleNotFound()
{
    String message = "Server is running!\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    server.send(200, "text/plain", message);
}

void setup()
{
    esp_err_t gpio_install_isr_service(int intr_alloc_flags);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    err = gpio_isr_handler_add(GPIO_NUM_13, &detectsMovement, (void *)13);
    if (err != ESP_OK)
    {
        Serial.printf("handler add failed with error 0x%x \r\n", err);
    }
    err = gpio_set_intr_type(GPIO_NUM_13, GPIO_INTR_POSEDGE);
    if (err != ESP_OK)
    {
        Serial.printf("set intr type failed with error 0x%x \r\n", err);
    }

    Serial.begin(115200);
    while (!Serial)
    {
        ;
    }

    int camInit = cam.init(esp32cam_aithinker_config);
    Serial.printf("Camera init returned %d\n", camInit);
    IPAddress ip;

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(F("."));
    }
    ip = WiFi.localIP();
    Serial.println(F("WiFi connected"));
    Serial.println(ip);

    server.on("/", HTTP_GET, handle_jpg_stream);
    server.on("/jpg", HTTP_GET, handle_jpg);
    server.on("/trigger", HTTP_GET, handle_alert_stream);
    server.onNotFound(handleNotFound);
    server.begin();
    rtspServer.begin();
}

CStreamer *streamer;
CRtspSession *session;
WiFiClient client; // FIXME, support multiple clients

void loop()
{
    if (trStatus == true){
        telClient.connect(ZM,6802);
        telClient.println("1|on+20|1|External Motion|External Motion");
        telClient.stop();
        trStatus = false;
    }
    
    uint8_t i;
    server.handleClient();

    uint32_t msecPerFrame = 100;
    static uint32_t lastimage = millis();

    // If we have an active client connection, just service that until gone
    // (FIXME - support multiple simultaneous clients)
    if (session)
    {
        session->handleRequests(0); // we don't use a timeout here,
        // instead we send only if we have new enough frames

        uint32_t now = millis();
        if (now > lastimage + msecPerFrame || now < lastimage)
        { // handle clock rollover
            session->broadcastCurrentFrame(now);
            lastimage = now;

            // check if we are overrunning our max frame rate
            now = millis();
            //if (now > lastimage + msecPerFrame)
            //printf("warning exceeding max frame rate of %d ms\n", now - lastimage);
        }

        if (session->m_stopped)
        {
            delete session;
            delete streamer;
            session = NULL;
            streamer = NULL;
        }
    }
    else
    {
        client = rtspServer.accept();

        if (client)
        {
            streamer = new OV2640Streamer(&client, cam); // our streamer for UDP/TCP based RTP transport

            session = new CRtspSession(&client, streamer); // our threads RTSP session and state
        }
    }
}