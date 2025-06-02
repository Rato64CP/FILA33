// rtc_vrijeme.h
#pragma once

void inicijalizirajRTC();
bool isDST(int dan, int mjesec, int danUTjednu);
void syncNTP();
void syncDCF();
extern String izvorVremena;
