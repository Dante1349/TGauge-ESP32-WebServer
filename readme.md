# T Gauge ESP32 Train Interface
## Parts needed:
* [ESP32-S3-DevKitC-1](https://amzn.to/4aESV0j)
* [L298N Motor Driver](https://amzn.to/3GV3A9p)
* [Power Distribution Board](https://amzn.to/3RVmvqY)
* [Jumper Wire](https://www.amazon.de/Female-Female-Male-Female-Male-Male-Steckbr%C3%BCcken-Drahtbr%C3%BCcken-bunt/dp/B01EV70C78?__mk_de_DE=%C3%85M%C3%85%C5%BD%C3%95%C3%91&crid=11ZJPAH13WTP&keywords=steck+kabel&qid=1703532073&sprefix=steck+kabel%2Caps%2C106&sr=8-5&linkCode=sl1&tag=dante1349-21&linkId=9d7f0c36daeacd3030640a6da1e3fb20&language=de_DE&ref_=as_li_ss_tl)
* [Power Connector](https://amzn.to/3TB5LXm)
* [5V Power Supply](https://amzn.to/41D6kSH)

## Installation
* install node: https://nodejs.org/en/download
* install @angular/cli: https://angular.io/cli
* install platformio cli: https://docs.platformio.org/en/stable/core/installation/index.html
* Connect ESP-Board
* Hold BOOT and press RESET to start download mode
* run in terminal `./uploadToESP32.sh`

# Pin config (D1 mini)
* *GPIO4* PWM pin
* *GPIO5* H-Bridge Pin 1
* *GPIO6* H-Bridge Pin 2

# Natural Lights
Natural lights can be defined in three types, houses, commercial buildings and street lights. These are the configured times how the lights behave: 

![light_time_table](assets/light_time_table.drawio.svg)

## Usage
* Look for WiFis and Connect to the "Train-Server-AP" Access Point. Enter your WiFi crendentials. The AP will automatically connect to your wifi.
* Find the "train" IP address in your Router.
* Open a browser and use the ip like following:
  * `http://*IP*/` or try `train.local`
