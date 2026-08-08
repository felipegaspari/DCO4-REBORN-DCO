// Seed last-fire stamps for both cores. Periods live as constexpr in Timer_micros.h.
void init_micros_timers() {
  const unsigned long now = micros();

  timer50micros = now;  timer50microsFlag = 0;
  timer51micros = now;  timer51microsFlag = 0;
  timer99micros = now;  timer99microsFlag = 0;
  // timer100micros = now;  timer100microsFlag = 0;
  // timer1ms = now;        timer1msFlag = 0;
  // timer223micros = now;  timer223microsFlag = 0;
  // timer5ms = now;        timer5msFlag = 0;
  // timer11ms = now;       timer11msFlag = 0;
  // timer23ms = now;       timer23msFlag = 0;
  // timer31ms = now;       timer31msFlag = 0;
  // timer67ms = now;       timer67msFlag = 0;
  // timer200ms = now;      timer200msFlag = 0;
  // timer500ms = now;      timer500msFlag = 0;
  // timer1000ms = now;     timer1000msFlag = 0;

  timer50micros2 = now;  timer50microsFlag2 = 0;
  timer51micros2 = now;  timer51microsFlag2 = 0;
  timer99micros2 = now;  timer99microsFlag2 = 0;
  // timer100micros2 = now;  timer100microsFlag2 = 0;
  // timer223micros2 = now;  timer223microsFlag2 = 0;
  //timer1ms2 = now;         timer1msFlag2 = 0;
  // timer5ms2 = now;        timer5msFlag2 = 0;
  // timer11ms2 = now;       timer11msFlag2 = 0;
  // timer23ms2 = now;       timer23msFlag2 = 0;
  // timer31ms2 = now;       timer31msFlag2 = 0;
  // timer67ms2 = now;       timer67msFlag2 = 0;
  // timer200ms2 = now;      timer200msFlag2 = 0;
  // timer500ms2 = now;      timer500msFlag2 = 0;
  // timer1000ms2 = now;     timer1000msFlag2 = 0;
}

// Core 0 — unrolled µs timers. Called every loop().
inline void microsTimer() {
  const unsigned long now = micros();

  timer50microsFlag = 0;  if (now - timer50micros > kTimer50us) { timer50micros = now; timer50microsFlag = 1; }
  timer51microsFlag = 0;  if (now - timer51micros > kTimer51us) { timer51micros = now; timer51microsFlag = 1; }
  timer99microsFlag = 0;  if (now - timer99micros > kTimer99us) { timer99micros = now; timer99microsFlag = 1; }
  // timer100microsFlag = 0;  if (now - timer100micros > kTimer100us) { timer100micros = now; timer100microsFlag = 1; }
  // timer1msFlag = 0;        if (now - timer1ms > kTimer1ms)         { timer1ms = now;       timer1msFlag = 1; }
  // timer223microsFlag = 0;  if (now - timer223micros > kTimer223us) { timer223micros = now; timer223microsFlag = 1; }
  // timer5msFlag = 0;        if (now - timer5ms > kTimer5ms)         { timer5ms = now;       timer5msFlag = 1; }
  // timer11msFlag = 0;       if (now - timer11ms > kTimer11ms)       { timer11ms = now;      timer11msFlag = 1; }
  // timer23msFlag = 0;       if (now - timer23ms > kTimer23ms)       { timer23ms = now;      timer23msFlag = 1; }
  // timer31msFlag = 0;       if (now - timer31ms > kTimer31ms)       { timer31ms = now;      timer31msFlag = 1; }
  // timer67msFlag = 0;       if (now - timer67ms > kTimer67ms)       { timer67ms = now;      timer67msFlag = 1; }
  // timer200msFlag = 0;      if (now - timer200ms > kTimer200ms)     { timer200ms = now;     timer200msFlag = 1; }
  // timer500msFlag = 0;      if (now - timer500ms > kTimer500ms)     { timer500ms = now;     timer500msFlag = 1; }
  // timer1000msFlag = 0;     if (now - timer1000ms > kTimer1000ms)   { timer1000ms = now;    timer1000msFlag = 1; }
}

// Core 1 — same periods, separate state (*2). Called every loop1().
inline void microsTimer2() {
  const unsigned long now = micros();

  //timer50microsFlag2 = 0;  if (now - timer50micros2 > kTimer50us) { timer50micros2 = now; timer50microsFlag2 = 1; }
  //timer51microsFlag2 = 0;  if (now - timer51micros2 > kTimer51us) { timer51micros2 = now; timer51microsFlag2 = 1; }
  timer99microsFlag2 = 0;  if (now - timer99micros2 > kTimer99us) { timer99micros2 = now; timer99microsFlag2 = 1; }
  // timer100microsFlag2 = 0;  if (now - timer100micros2 > kTimer100us) { timer100micros2 = now; timer100microsFlag2 = 1; }
  timer1msFlag2 = 0;         if (now - timer1ms2 > kTimer1ms)         { timer1ms2 = now;       timer1msFlag2 = 1; }
  // timer223microsFlag2 = 0;  if (now - timer223micros2 > kTimer223us) { timer223micros2 = now; timer223microsFlag2 = 1; }
  // timer5msFlag2 = 0;        if (now - timer5ms2 > kTimer5ms)         { timer5ms2 = now;       timer5msFlag2 = 1; }
  // timer11msFlag2 = 0;       if (now - timer11ms2 > kTimer11ms)       { timer11ms2 = now;      timer11msFlag2 = 1; }
  // timer23msFlag2 = 0;       if (now - timer23ms2 > kTimer23ms)       { timer23ms2 = now;      timer23msFlag2 = 1; }
  // timer31msFlag2 = 0;       if (now - timer31ms2 > kTimer31ms)       { timer31ms2 = now;      timer31msFlag2 = 1; }
  // timer67msFlag2 = 0;       if (now - timer67ms2 > kTimer67ms)       { timer67ms2 = now;      timer67msFlag2 = 1; }
  // timer200msFlag2 = 0;      if (now - timer200ms2 > kTimer200ms)     { timer200ms2 = now;     timer200msFlag2 = 1; }
  // timer500msFlag2 = 0;      if (now - timer500ms2 > kTimer500ms)     { timer500ms2 = now;     timer500msFlag2 = 1; }
  // timer1000msFlag2 = 0;     if (now - timer1000ms2 > kTimer1000ms)   { timer1000ms2 = now;    timer1000msFlag2 = 1; }
}
