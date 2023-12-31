import {Component, OnDestroy, OnInit} from '@angular/core';
import {CommonModule} from '@angular/common';
import {RouterOutlet} from '@angular/router';
import {HttpClient} from "@angular/common/http";
import {FormsModule} from "@angular/forms";
import {Subscription} from "rxjs";

enum Season {
  Christmas = 'Christmas',
  Easter = 'Easter',
  Halloween = 'Halloween',
  NewYearsEve = 'New Year\'s Eve',
  Default = 'Default'
}

enum ComponentType {
  Background = 'Background',
  Text = 'Text',
  Button = 'Button',
  Heading = 'Heading',
  Slider = 'Slider',
}

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, RouterOutlet, FormsModule],
  templateUrl: './app.component.html'
})
export class AppComponent implements OnInit, OnDestroy {
  private subscriptions: Subscription[] = [];
  speed: number = 0;
  speedLimit: number = 255;
  ip: string = '127.0.0.1';
  realSpeed: number = 0;
  COMPONENT_TYPE = ComponentType;
  private webSocket: WebSocket;

  constructor(private http: HttpClient) {
  }

  ngOnInit() {
    this.subscriptions.push(
      this.http.get('getSpeedLimit', {responseType: "text"}).subscribe((sL) => {
        if (sL !== "" && Number(sL) > 0 && Number(sL) < 256) {
          this.speedLimit = Number(sL);
        }
      }),
      this.http.get('getLocalIP', {responseType: "text"}).subscribe((ip) => {
          this.ip = ip;
          this.initWebSocket(ip);
      })
    )



    /*const i = 250;
    setInterval(() => {
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=00000001').subscribe(() => {})},i)
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=00000010').subscribe(() => {})},i*2)
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=00000100').subscribe(() => {})},i*3)
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=00001000').subscribe(() => {})},i*4)
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=00010000').subscribe(() => {})},i*5)
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=00100000').subscribe(() => {})},i*6)
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=01000000').subscribe(() => {})},i*7)
      setTimeout(()=>{this.http.get('/config?brightness=100&leds=10000000').subscribe(() => {})},i*8)
    }, i*8)*/
  }

  ngOnDestroy() {
    this.unsubscribeAll()
  }

  initWebSocket(ip: string) {
    this.webSocket = new WebSocket(`ws://${ip}:81`);

    this.webSocket.onopen = () => {
      console.log('WebSocket connected');
    };

    this.webSocket.onmessage = (event) => {
      console.log('Received message:', event.data);
      this.realSpeed = event.data;
      this.speed = this.mapValueTo100(this.realSpeed);
    };

    this.webSocket.onclose = () => {
      console.log('WebSocket connection closed');
    };
  }

  private unsubscribeAll(): void {
    this.subscriptions.forEach(s => s.unsubscribe())
  }

  private mapValueTo255(input: number): number {
    const clampedInput = Math.min(Math.max(input, 0), 100)
    return Math.round((clampedInput / 100) * this.speedLimit);
  }

  private mapValueTo100(input: number): number {
    const clampedInput = Math.min(Math.max(input, 0), this.speedLimit)
    return Math.round((clampedInput / this.speedLimit) * 100);
  }

  changeSpeed(): void {
    this.realSpeed = this.mapValueTo255(this.speed)
    const url = `/config?speed=${String(this.realSpeed)}`
    this.http.get(url, {responseType: 'text'}).subscribe((next) => {
      console.log('HTTP request successful:', next);
    })
  }

  reverseDirection(): void {
    const url = `/reverse`
    this.http.get(url, {responseType: 'text'}).subscribe((next) => {
      console.log('HTTP request successful:', next);
    })
  }

  // Meeus/Jones/Butcher Gregorian algorithm
  private calculateEasterDate(year: number): Date {
    const a = year % 19;
    const b = Math.floor(year / 100);
    const c = year % 100;
    const d = Math.floor(b / 4);
    const e = b % 4;
    const f = Math.floor((b + 8) / 25);
    const g = Math.floor((b - f + 1) / 3);
    const h = (19 * a + b - d - g + 15) % 30;
    const i = Math.floor(c / 4);
    const k = c % 4;
    const l = (32 + 2 * e + 2 * i - h - k) % 7;
    const m = Math.floor((a + 11 * h + 22 * l) / 451);
    const month = Math.floor((h + l - 7 * m + 114) / 31);
    const day = ((h + l - 7 * m + 114) % 31) + 1;

    return new Date(year, month - 1, day);
  }

  public getCurrentSeason(): Season {
    const currentDate: Date = new Date();
    const currentMonth: number = currentDate.getMonth() + 1; // Month is zero-indexed
    const currentDay: number = currentDate.getDate();
    const easterDate: Date = this.calculateEasterDate(currentDate.getFullYear());
    const easterMonth: number = easterDate.getMonth() + 1;
    const easterDay: number = easterDate.getDate();

    switch (currentMonth) {
      case 3: // March for Easter
        if ((currentMonth === easterMonth && currentDay >= easterDay - 2)) {
          return Season.Easter;
        }
        break;
      case 4: // April for Easter
        if ((currentMonth === easterMonth && currentDay >= easterDay - 2)) {
          return Season.Easter;
        }
        break;
      case 10: // October for Halloween
        if (currentDay >= 28 && currentDay <= 31) {
          return Season.Halloween;
        }
        break;
      case 12: // December for Christmas and New Year's Eve
        if (currentDay >= 1 && currentDay <= 30) {
          return Season.Christmas;
        } else if (currentDay >= 31 && currentDay <= 31) {
          return Season.NewYearsEve;
        }
        break;
    }

    return Season.Default; // Default or any other season
  }

  getSeasonalClass(componentType: ComponentType): string {
    let type = ""
    switch (componentType) {
      case ComponentType.Background:
        type = "background"
        break;
      case ComponentType.Text:
        type = "text"
        break;
      case ComponentType.Button:
        type = "button"
        break;
      case ComponentType.Heading:
        type = "heading"
        break;
      case ComponentType.Slider:
        type = "slider"
        break;
      default:
        console.error(`Component type ${componentType} unknown`);
        return ""
    }

    switch (this.getCurrentSeason()) {
      case Season.Easter:
        return `${type}__easter`;
      case Season.Halloween:
        return `${type}__halloween`;
      case Season.Christmas:
        return `${type}__christmas`;
      case Season.NewYearsEve:
        return `${type}__new-year`;
      case Season.Default:
        return `${type}__default`;
      default:
        return "";
    }
  }

  getHeadingText() {
    switch (this.getCurrentSeason()) {
      case Season.Christmas:
        return "(Christmas Train)";
      case Season.Easter:
        return "Easter Train";
      case Season.Halloween:
        return "Halloween Train";
      case Season.NewYearsEve:
        return "New Year Train";
      case Season.Default:
        return "Train";
      default:
        console.error("Cannot detect current season");
        return "Train"
    }
  }
}
