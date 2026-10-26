# Chapter 10: Tea Race 1866

> **App Version**: v3.0.0  
> **Last Verified**: 2026-10-26  

---

A short (10-15 minute), touch-only sailing strategy game. Captain the clipper *Taeping* in the Great Tea Race of 1866, from Foochow to London, against *Ariel*, *Fiery Cross* and *Serica*.

## How a voyage plays
1. **Choose your cargo.** Fine first-flush pays the most if you are first home but falls away fast if you are beaten; congou pays less but is steadier.
2. **Nine legs.** Each leg shows the race standings, your ship (hull & rigging, crew, wet chests) and **the glass**, a forecast that is right about seven times in ten.
3. **Pick a sail plan.** *Shorten sail*, *Steady as she goes* or *Crack on!* Each button shows the expected days, the chance of damage and the effect on the crew, for the forecast weather.
4. **See what the weather actually did**: days taken, damage, water in the hold, rival news.
5. **Dilemmas** turn up on about a third of legs: pirates, fever, a leak, becalming, a rival on the horizon, an albatross, a grumbling crew, a whaler, and a Channel pilot.
6. **Ports.** Put into Anjer or St Helena to repair (2.5 days, £40), or sail on.
7. **London.** The owners' account: the price for your finishing place, minus wet chests, a stale cargo after a slow passage, and expenses. Your best voyage is remembered.

## Design notes
- A damaged ship is slower and a tired crew makes more mistakes, so pushing hard early makes later legs harder. Repairs cost days and money.
- Balanced with a Monte Carlo model: sensible play averages about £3,100 and wins roughly a third of races; always cracking on is a high-variance gamble; always shortening sail never wins.
- The back chevron returns to the title screen, which offers *Continue voyage*; nothing is lost by a stray tap.
- Code: `src/apps/TeaRaceApp.cpp`. Best score: `/data/tearace_best.txt`.
