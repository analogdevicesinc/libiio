# Contributors

Since it's first commit on 17 Feb 2014, the libIIO has seen many improvements by many contributors.
Regular releases every 6 months delivers stable updates to users and developers,
each with new features, new examples, added device support, and improved performance. 
Each of these releases contains the work of over 45 developers representing over 25 corporations.

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
|   Paul Cercueil         |    17904 
|   Robin Getz            |     7932 
|   Lars-Peter Clausen    |     3785 
|   Tim Harder            |     2259 
|   Michael Hennerich     |     1175 
|   Matt Fornero          |      987 
|   Travis F. Collins     |      602 
|   Alexandru Ardelean    |      555 
|   Cristi Iacob          |      437 
|   Lucas Magasweran      |      358 
|   Matej Kenda           |      273 
|   Alexandra Trifan      |      257 
|   Romain Roffé          |      225 
|   Geert Uytterhoeven    |       35 
|   Andrea Galbusera      |       32 
|   JaredD                |       31 
|   Adrian Freihofer      |       31
|   Dan Nechita           |       29 
|   Adrian Suciu          |       23 
|   Petr Štetiar          |       20 
|   Edward Kigwana        |       19 
|   Andreas Brauchli      |       18 
|   fpagliughi            |       16 
|   Samuel Martin         |       12 
|   Rémi Lefèvre          |       12 
|   Michael Heimpold      |       12 
|   SrikanthPagadarai     |       11 
|   Dimas Abreu Archanjo Dutra  |        9 
|   Marc Titinger         |        8 
|   Jonas Hansen          |        4 
|   Jeremy Trimble        |        4 
|   David Frey            |        4 
|   Ryo Hashimoto         |        3 
|   Markus Gnadl          |        2 
|   Julien Malik          |        2 
|   Jorik Jonker          |        2 
|   Pierre-Jean Texier    |        1 
|   Nicholas Pillitteri   |        1 
|   Morten Fyhn Amundsen  |        1 
|   Johnny Vestergaard    |        1 
|   Gwendal Grignou       |        1 
|   Ben Acland            |        1 


## Domains (or companies) contributing to the libIIO

In order of most contributions to least (counted by lines of code).

| Company          | Lines of code
| ---------------- | -------------
|  analog.com       |  31153
|  metafoo.de       |  3785
|  mathworks.com    |  987
|  gmail.com        |  382
|  daqri.com        |  358
|  parrot.com       |  237
|  linux-m68k.org   |  35
|  scs.ch           |  22
|  true.cz          |  20
|  sensirion.com    |  18
|  mindspring.com   |  16
|  heimpold.de      |  12
|  ufmg.br          |  9
|  baylibre.com     |  8
|  sierrawireless.com  |  4
|  azuresummit.com   |  4
|  google.com        |  3
|  paraiso.me        |  2
|  kippendief.biz    |  2
|  iabg.de           |  2
|  unixcluster.dk    |  1
|  koncepto.io       |  1
|  crapouillou.net   |  1
|  chromium.org      |  1

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
