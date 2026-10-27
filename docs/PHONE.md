# iPhone over Bluetooth (v0.7.0)

Ingen app og intet wifi. Boardet er stadig forbundet til løbehjulet som før (centralrolle) og
optræder samtidig som en Bluetooth-enhed for din iPhone (perifer rolle).

## Parring (én gang)
1. På boardet: Settings → **PHONE ON**. Skærmen viser PHONE: WAITING på MUSIC-skærmen.
2. iPhone: Indstillinger → Bluetooth → vælg **NOVA RIDE** → Parr.
3. Når MUSIC viser **PHONE: CONNECTED**, virker PREV / PLAY-PAUSE / NEXT. Dashboardet viser PHONE øverst.

Knapperne sendes som standard Bluetooth-medietaster og styrer den app, der afspiller (Apple Musik, Spotify, osv.).
Hvis iPhonen har problemer med at parre: glem enheden i iPhone-indstillingerne, sluk og tænd PHONE ON/OFF og prøv igen.
iOS kan behandle enheden som et fjernbetjenings-tastatur; det kan fx skjule skærmtastaturet, mens den er forbundet.

## Hvad der IKKE er lavet endnu
* **Titel og kunstner** (Apple Media Service) – kræver GATT-klient-kode mod iPhonen.
* **Beskeder** (Apple Notification Center Service) – samme.
Begge bygges som næste trin, når parring og medietaster er bekræftet på din telefon.

## Ikke testet
Alt ovenfor er kun syntakstjekket. Det er ikke prøvet på boardet eller med en iPhone, og jeg har ikke
bekræftet, at scooter-forbindelsen og telefonforbindelsen kører stabilt samtidig. Slå PHONE fra, hvis scooter-linket driller.
