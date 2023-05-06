# Contributors

Since it's first commit on 17 Feb 2014, the libIIO has seen many improvements by many contributors.
Regular releases every ~6 months delivers stable updates to users and developers,
each with new features, new examples, added device support, and improved performance. 
Each of these releases contains the work of over 55+ developers representing over 30+ organizations.

Although the libIIO would not exist without the initial support of Analog Devices,
it is an open source source project, and relies on the contributions of many people.
Being used across the cloud, mobile, embedded, and supercomputing applications, ensures 
that it supports many different use cases and applications.

The numbers in the following tables are drawn from the entire git repository history.
Periodically (at least once a release), we run a script included in this file to update this file.
This does not include the people working on the kernel side, writing the actual Linux kernel IIO device drivers, 
or tiny-iiod the deeply embedded iio daemon.

## Individuals contributing to the libIIO

| Author                  | Lines of Code
| ---------------------   | -------------
| Paul Cercueil           |  24510
| Robin Getz              |  12624
| Lars-Peter Clausen      |   3003
| Raluca Chis             |   1233
| Cristi Iacob            |   1055
| Michael Hennerich       |    878
| Alexandru Ardelean      |    835
| Matt Fornero            |    768
| Iacob                   |    588
| Travis F. Collins       |    577
| Nuno Sá                 |    531
| Lucas Magasweran        |    347
| Romain Roffé            |    202
| Matej Kenda             |    193
| Adrian Suciu            |    140
| Mihail Chindris         |     91
| AlexandraTrifan         |     85
| Dan Nechita             |     52
| Geert Uytterhoeven      |     36
| Adrian Freihofer        |     30
| JaredD                  |     28
| RChis1                  |     25
| Andrea Galbusera        |     25
| Petr Štetiar            |     20
| Andreas Brauchli        |     18
| fpagliughi              |     15
| Fabrice Fontaine        |     14
| Rémi Lefèvre            |     12
| Edward Kigwana          |     12
| Samuel Martin           |     11
| SrikanthPagadarai       |     10
| Max Lehuraux            |      9
| Julien Malik            |      9
| Dimas Abreu Archanjo Dutra  |  9
| Marc Titinger           |      8
| Marvin Schmidt          |      7
| Kathy Camenzind         |      6
| Chris Lamb              |      6
| Tim Harder              |      5
| Misko                   |      4
| Michael Heimpold        |      4
| Jeremy Trimble          |      4
| f4exb                   |      4
| David Frey              |      4
| DanielGuramulta         |      4
| Gwendal Grignou         |      3
| Matt Thomas             |      2
| Markus Gnadl            |      2
| Jan Tojnar              |      2
| Cormier, Jonathan       |      2
| Virgil Litan            |      1
| Pierre-Jean Texier      |      1
| Nicholas Pillitteri     |      1
| Morten Fyhn Amundsen    |      1
| Marc Sporcich           |      1
| Jonas Hansen            |      1
| Johnny Vestergaard      |      1


## Domains (or companies) contributing to the libIIO

In order of most contributions to least (counted by lines of code).

| Company             | Lines of code
| ------------------- | -------------
| analog.com          |  33846
| crapouillou.net     |   9205
| metafoo.de          |   3003
| mathworks.com       |    824
| gmail.com           |    413
| daqri.com           |    347
| parrot.com          |    214
| linux-m68k.org      |     36
| scs.ch              |     22
| true.cz             |     20
| sensirion.com       |     18
| mindspring.com      |     15
| ufmg.br             |      9
| baylibre.com        |      8
| unseenlabs.fr       |      7
| exherbo.org         |      7
| tulip.co            |      6
| chris-lamb.co.uk    |      6
| sierrawireless.com  |      4
| heimpold.de         |      4
| cs.toronto.edu      |      4
| azuresummit.com     |      4
| chromium.org        |      3
| users.github.com    |      2
| paraiso.me          |      2
| iabg.de             |      2
| criticallink.com    |      2
| unixcluster.dk      |      1
| scires.com          |      1
| koncepto.io         |      1
| epiqsolutions.com   |      1

## scripts
scripts were based on a [gist](https://gist.github.com/amitchhajer/4461043) from Amit Chhajer.

Measuring authors:
```sh
git ls-files | while read f; do git blame -w --line-porcelain -- "$f" | grep -I '^author '; done | sort -f | uniq -ic | sort -nr | sed 's/author/|/' | awk -F'|' '{print "| ", $2, " | ", $1}'
```
After this is done, the list is manipulated manually - not everyone uses the same name for every commit on every development machine.

Measuring domains:

```sh
git ls-files | while read f; do git blame -w --line-porcelain -- "$f" | grep -I '^author-mail'; done | sort -f | uniq -ic | sort -nr | sed -e 's/<.*@//' -e 's/>$//' -e 's/author-mail//' | sort -k 2 | awk '{arr[$2]+=$1} END {for (i in arr) {print "| ", i," | ",arr[i]}}' |  sort -k 4 -nr
```
