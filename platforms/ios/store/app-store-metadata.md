# App Store metadata (draft)

A draft of what App Store Connect asks for, for the day the game is submitted. Nothing here is
read by the build. Today players build the game themselves and install it on their own devices
([../README.md](../README.md)); this page is for a later TestFlight or App Store release.

## Name and subtitle

- **Name:** Open Annihilation
- **Subtitle:** Real-time strategy with your own game files
- **Bundle ID:** net.coreprime.open-annihilation
- **Primary category:** Games, with the subcategory Strategy. **Secondary category:** none.

## Description

> Open Annihilation is a free, open source engine for playing the real-time strategy game Total
> Annihilation on iPhone and iPad. Build a base, gather metal and energy, and command hundreds of
> units across land, sea and air in skirmishes against computer players and in the campaigns.
>
> Open Annihilation does not include the game. It plays from your own copy of Total
> Annihilation: copy your game folder into Open Annihilation's folder with the Files app or the
> Finder, and the game starts from it. The 1997 demo's installer works too.
>
> Made for touch: tap to select and order, drag a box around units, pinch to zoom, hold for the
> order menu, and place buildings with a lifted ghost and a check mark. On iPad you get the full
> interface with a thumb column and quick buttons; on iPhone a compact layout keeps the
> battlefield large. A hardware keyboard, mouse, trackpad or Apple Pencil works too.
>
> Play against friends on the same local network.
>
> Open Annihilation collects no data, has no accounts, no adverts and no purchases.

## Keywords

`strategy,RTS,real-time strategy,Total Annihilation,skirmish,campaign,robots,base building,LAN`

(100 characters at most, separated by commas; Total Annihilation appears only because the game
needs the player's own copy.)

## Promotional text

> Bring your copy of Total Annihilation to iPhone and iPad, with touch controls made for it.

## Support and marketing

- **Support URL:** the project's issue tracker (fill in).
- **Marketing URL:** the project's home page (fill in).
- **Copyright:** The Open Annihilation Authors.

## App privacy

- **Data collection:** Data Not Collected. The game sends nothing to its developers or to anyone
  else; LAN games talk only to the other players' devices on the local network.
- **Tracking:** none. The bundle's privacy manifest (PrivacyInfo.xcprivacy, from
  [../PrivacyInfo.plist](../PrivacyInfo.plist)) says the same and gives the reasons for the system
  calls Apple asks about.
- **Privacy policy URL:** required even for Data Not Collected; a short page saying the game
  collects nothing (fill in).

## Export compliance

The game uses no encryption beyond the system's own (it checks files with SHA-256, which is
hashing, not encryption). Info.plist sets `ITSAppUsesNonExemptEncryption` to false, so App Store
Connect does not ask on every upload.

## Age rating notes

Answers for the age rating questionnaire, as the game plays:

- **Cartoon or fantasy violence:** Frequent/Intense. Armies of robot war machines fight and
  explode throughout every game; no people are shown being hurt and there is no blood.
- **Realistic violence, sexual content, nudity, profanity, horror, drugs, alcohol, tobacco,
  gambling, contests:** None.
- **Unrestricted web access:** No.
- **Messaging and chat:** Yes, limited: players in a LAN game can send each other text messages,
  only between devices on the same local network; nothing goes over the internet.
- **User-generated content:** No sharing service; players may load maps and mods they copy into
  the game's folder themselves.

The expected result is 9+ (fantasy violence); check it against the questionnaire's current
wording when submitting.

## Notes for App Review

> Open Annihilation plays the 1997 PC game Total Annihilation from the player's own copy and
> ships no game data. To review it without a copy, use the game's free 1997 demo:
>
> 1. On the device, download the demo's installer (a 21.5 MB Windows file,
>    "Total Annihilation.exe") in Safari from (fill in a link); it is saved to Files ›
>    Downloads.
> 2. Open Open Annihilation. It opens on its Game files screen: tap Play the 1997 demo ›
>    Choose installer, then choose the downloaded file, and tap Copy.
> 3. The app recognises the file by its checksum, never runs it, and unpacks its game data;
>    tap Play to open the game's main menu. A recording of these steps is attached.
> 4. Choose SINGLE, then Skirmish, then Start, to play a skirmish against a computer player.
>
> Without game files the app stays on the Game files screen, which offers the ways in; it never
> closes for lack of them.
>
> Multiplayer is over the local network only and asks for local network access when a LAN
> game is opened. The game has no account, no sign-in, no purchases and no network service.

## Screenshots

App Store Connect needs screenshots for the 6.9-inch iPhone and the 13-inch iPad display
sizes, landscape. Take them from a skirmish on the simulators (`xcrun simctl io <device>
screenshot <file>` gives the portrait screen, so turn the image a quarter turn) with the
player's own game data; no game art may appear that the player's copy does not provide.
