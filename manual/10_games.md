# Chapter 10: Games

> **App Version**: v3.0.0  
> **Last Verified**: 2026-10-26  

The **Games** tile on the home screen opens a list of short, touch-only games. Leaving a game returns to this list. Each game remembers your best result.

---

## Tea Race 1866

A short (10-15 minute), touch-only sailing strategy game. Captain the clipper *Taeping* in the Great Tea Race of 1866, from Foochow to London, against *Ariel*, *Fiery Cross* and *Serica*.

### How a voyage plays
1. **Choose your cargo.** Fine first-flush pays the most if you are first home but falls away fast if you are beaten; congou pays less but is steadier.
2. **Nine legs.** Each leg shows the race standings, your ship (hull & rigging, crew, wet chests) and **the glass**, a forecast that is right about seven times in ten.
3. **Pick a sail plan.** *Shorten sail*, *Steady as she goes* or *Crack on!* Each button shows the expected days, the chance of damage and the effect on the crew, for the forecast weather.
4. **See what the weather actually did**: days taken, damage, water in the hold, rival news.
5. **Dilemmas** turn up on about a third of legs: pirates, fever, a leak, becalming, a rival on the horizon, an albatross, a grumbling crew, a whaler, and a Channel pilot.
6. **Ports.** Put into Anjer or St Helena to repair (2.5 days, £40), or sail on.
7. **London.** The owners' account: the price for your finishing place, minus wet chests, a stale cargo after a slow passage, and expenses. Your best voyage is remembered.

### Design notes
- A damaged ship is slower and a tired crew makes more mistakes, so pushing hard early makes later legs harder. Repairs cost days and money.
- Balanced with a Monte Carlo model: sensible play averages about £3,100 and wins roughly a third of races; always cracking on is a high-variance gamble; always shortening sail never wins.
- The back chevron returns to the title screen, which offers *Continue voyage*; nothing is lost by a stray tap.
- Code: `src/apps/TeaRaceApp.cpp`. Best score: `/data/tearace_best.txt`.

---

## The Keeper

Winter, 1891: twenty-one days alone on the Skerry lighthouse until the relief boat comes.

- **Days**: two tasks a day. *Fish* (less in rough weather), *gather fuel* (wreckage washes up after a storm), *trim the lamp*, *rest*, or spend the whole day *rowing to the harbour* for oil, food, fuel and a rocket (calm or fresh weather only; a fresh breeze can swamp the boat).
- **Nights**: the lamp burns oil (more in storm and fog), the stove burns fuel (more in a gale), and you eat. *Keep watch* (or ring the fog bell) to catch a guttering lamp, at a cost to your spirit, or *sleep* and hope.
- **Ships**: when the light is dark, or the fog bell is silent, ships strike the reef. In gales a ship may come into distress: fire rockets, launch the boat, or watch and pray.
- **Spirit**: hunger, cold and long watches wear you down; if your spirit breaks you are taken off the rock.
- **Score**: ships guided safely, lives saved, minus ships lost. Balanced with a Monte Carlo model (careful play survives almost every winter with about two wrecks).
- Code: `src/apps/KeeperApp.cpp`. Best score: `/data/keeper_best.txt`.

---

## Codebreaker 1917

Room 40, 1917: recover the enemy's cipher key before the attack. It plays like Mastermind.

- Each guess costs a day; you have ten. After each guess, solid dots count symbols in the right place and hollow dots count right symbols in the wrong place.
- Tap a slot, then a symbol; *Send* when the row is full.
- **Recruit**: 4 symbols from A-F, no repeats. **Analyst**: 4 from A-H, repeats allowed. **Room 40**: 5 from A-H, repeats allowed.
- Break the key and the intercepted dispatch is decrypted; fail and the key is revealed along with what the attack did.
- Service record (played, won, streaks, fewest days per difficulty) and the game in progress are kept in `/data/codebreaker.txt`. Abandoning a key part-way counts as a loss.
- Code: `src/apps/CodebreakerApp.cpp`.
