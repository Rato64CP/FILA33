// ups_nadzor.h - Nadzor mreznog napona i UPS moda toranjskog sata
#pragma once

// Inicijalizira ulaz za nadzor mreze i pocetno stanje UPS moda.
void inicijalizirajUPSNadzor();

// Periodicki osvjezava stanje mreze i primjenjuje blokade mehanike dok sat radi s UPS-a.
void osvjeziUPSNadzor();

// Vraca true kad je UPS mod ukljucen i mreza nije prisutna.
bool jeUPSModAktivan();
