import {Component, OnDestroy, OnInit} from '@angular/core';
import {CommonModule} from '@angular/common';
import {RouterOutlet} from '@angular/router';
import {HttpClient} from "@angular/common/http";
import {FormsModule} from "@angular/forms";
import {interval, Subscription, switchMap} from "rxjs";

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, RouterOutlet, FormsModule],
  templateUrl: './app.component.html'
})
export class AppComponent implements OnInit, OnDestroy {
  private readonly SPEED_LIMIT: number = 120 // has to be a value from 0 to 255
  private subscriptions: Subscription[] = []
  speed: number = 0;
  realSpeed: number = 0;

  constructor(private http: HttpClient) {
  }

  ngOnInit() {
    this.subscriptions.push(this.http.get('getSpeed', {responseType: 'text'}).subscribe((speed) => {
      this.speed = this.mapValueTo100(Number(speed))
      this.realSpeed = Number(speed)
    }))

    // in v2 -> d1 mini is too slow to handle that many requests
    /*this.subscriptions.push(interval(1000)
      .pipe(
        switchMap(() => this.http.get('getSpeed', {responseType: 'text'}))
      )
      .subscribe((speed: string) => {
        console.log(speed)
        this.speed = this.mapValueTo100(Number(speed))
        this.realSpeed = Number(speed)
      }))*/
  }

  ngOnDestroy() {
      this.subscriptions.forEach(s => s.unsubscribe())
  }

  private mapValueTo255(input: number): number {
    const clampedInput = Math.min(Math.max(input, 0), 100)
    return Math.round((clampedInput / 100) * this.SPEED_LIMIT);
  }

  private mapValueTo100(input: number): number {
    const clampedInput = Math.min(Math.max(input, 0), this.SPEED_LIMIT)
    return Math.round((clampedInput / this.SPEED_LIMIT) * 100);
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
}
