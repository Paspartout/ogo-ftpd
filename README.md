ogo-ftpd
========

ogo-ftpd is a minimal [ftp] server for the [odroid-go].

The use case I had in mind is the transfer of files in a trusted local network.
So no authentication is supported yet.

It uses my [uftpd] library. Take a look at it for the limitations of the server.

Usage
-----

### Download

You should be able to download the fw file from [Githubs releases][releases].

### Configuration

In order to connect to your wifi network the application reads the ssid and password
from the sd card. You have to place it in the root folder of the sd card and
name it `wifi.json`. The contents need to be as follows:

```json
{"networks": [{"ssid": "YOUR_SSID", "password": "YOUR_PASSWOD", "authmode": "YOUR_AUTHMODE"}]}
```

Here YOUR_AUTHMODE can be one of `open`, `wep`, `wpa-psk`, `wpa2-psk` or `wpa/wpa2-psk`.
You can also add multiple networks by adding them to the json array.

#### Note

I might add a gui with onscreen keyboard for configuration in the future, 
so you don't need to do this manually.
	  
If you happen to use the [odroid-go-launcher] from [Jeff Kent] you can already do
that. Just make sure to backup the configuration to the sd card.

### Running

After the app is started the odroid will attempt to connect to your wifi and once
it got an ip address it will start the ftp server.
Once started you can connect to it by using the displayed ip address and port 21.
No authentication is required yet(any username and password will do).
I suggest using ftp anonymous login.

Building
--------

For building I used the [esp-idf-fork] from [crashoverdrive] that contains some
needed fixes for the odroid-go. It might be worth trying out a newer version
with the patches ported.

So, to build the server you need to have the [esp-idf installed][esp-idf-setup].

In addition to that to build the .fw file you need to have [mkfw] installed.
Then simply run `make dist` to make the .fw file.
Take a look at the [Makefile](Makefile) to see how it works and if you want
to change the path to the mkfw utility.

Acknowledgements
----------------

I used the [hello-world-app] from [Jeff Kent] which uses his [hardware-lib]
to handle the odroids hardware. It made the development easier even though
I had to modify some functions to provide me with error context. Thanks!
Take a look at [LICENSE.template](LICENSE.template) for the license of his code.

[ftp]: https://en.wikipedia.org/wiki/File_Transfer_Protocol
[uftpd]: https://github.com/Paspartout/uftpd
[Jeff Kent]: https://github.com/jkent
[odroid-go]: https://wiki.odroid.com/odroid_go/odroid_go
[hello-world-app]: https://github.com/jkent/odroid-go-hello-world-app
[hardware-lib]: https://github.com/jkent/odroid-go-hardware-lib
[esp-idf-fork]: https://github.com/OtherCrashOverride/esp-idf/
[crashoverdrive]: https://github.com/OtherCrashOverride
[esp-idf-setup]: https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html
[mkfw]: https://github.com/OtherCrashOverride/odroid-go-firmware/tree/master/tools/mkfw
[releases]: https://github.com/Paspartout/ogo-ftpd/releases
[odroid-go-launcher]: https://github.com/jkent/odroid-go-launcher
