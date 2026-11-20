# PicoDeck app store (assets.msuii.dev/picopadapps)

Live files are served by nginx on `msuii` from:

    /var/www/assets/picopadapps/

with this location block in
`/etc/nginx/sites-available/assets.msuii.dev.conf`:

    location ^~ /picopadapps/ {
        alias /var/www/assets/picopadapps/;
        autoindex off;
        default_type application/octet-stream;
        types { text/plain txt; application/octet-stream pap; }
    }

`index.txt` format (one app per line):

    id|version|size|url|name

Publish updated packages:

    scp index.txt *.pap root@85.255.2.79:/var/www/assets/picopadapps/

The desktop companion's default STORE_URL already points at:

    https://assets.msuii.dev/picopadapps/index.txt

The PicoPad talks to the always-on companion over USB CDC; the companion
fetches HTTPS and streams the package back.
