# EggTimer
A few years ago, a friend of mine came up with an algorithm for "the perfectly boiled egg". Well, naturally I could not resist building an egg timer that after weighing the egg calculated the boiling time according to his algorithm and did the count down. Admittedly, what constitutes a prefectly boiled egg may differ from person to person though, so I did make it possible to slightly adjust the boiling time. You know, just in case...

## The algorithm
The somewhat simple algorithm is: the weight of the egg in grams * 225/34 + 15, with the result expressed in seconds. I guess that just as with [Columbus' egg](https://en.wikipedia.org/wiki/Egg_of_Columbus "Columbus' Egg on Wikipedia"), the hard part is to come up with it.

## Usage
The device is simple; you press the button, place an egg, pick it up again, and push the button again at the moment you place the egg in the boiling water. When the buzzer sounds you pick the egg up and enjoy it.

## Disclaimers
Yes, we all know that it's not only the weight of the egg that determines the perfect boiling time. Other factors as the shape of the egg, how old it is, if you eat it directly after picking it up from the boiling water or let it wait a bit, the air preassure and ambient temperature - just to name a few - are all very valid parameters that would have to be taken into account. But hey; "aim for the stars and you'll reach the tree tops", right?

## Material
This is the material I used when building this device:
1. Rechargeable Li-Po battery, 2000 mAh
2. Li-Po charger, mounted on break out board
3. Atmega 328P
4. Load cell
5. HX711 load cell amplifier board, mounted on break out board
6. 4 digit, 7 segment LCD display, with accompanying TM1673 controller chip
7. A variety of wires and passive and active components (se image below)
8. An old box that came with a 4th generation iPod nano (that now is long gone)
9. A cap to a PET bottle

## The instructions
First a few prerequisites. This project does not explain all the details, such as how you program the 328P chip, solder or set up the development environment as there are countless of instructions on how this is done.

