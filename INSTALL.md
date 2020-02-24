## basic setup


   Client binary       ------network------    ATS     ------network-------   nginx + origin
   
Recommend OS: Ubuntu 18.04
   
### on proxy
* Install [LRB simulator](https://github.com/sunnyszy/lrb) library.
* Install ATS
```shell script
sudo apt install libhwloc-dev libhwloc5 libunwind8 libunwind8-dev google-perftools
sudo apt install autoconf automake autotools-dev bison debhelper dh-apparmor flex gettext intltool-debian libbison-dev libcap-dev libexpat1-dev libfl-dev libpcre3-dev libpcrecpp0v5 libsigsegv2 libsqlite3-dev libssl-dev libtool m4 po-debconf tcl-dev tcl8.6-dev zlib1g-dev
git clone https://github.com/sunnyszy/lrb-prototype ~/webtracereplay
cd ~/webtracereplay/trafficserver-8.0.3
autoreconf -if
make clean
./configure --prefix=/opt/ts
make -j32
make install
# decide on a username
sudo chown -R ${YOUR USERNAME} /opt/ts
# delete default config dir (or backup..)
sudo rm -rf /opt/ts/etc/trafficserver
# link this repo's config dir
ln -s ~/webtracereplay/tsconfig /opt/ts/etc/trafficserver
```

## on origin
```shell script
sudo apt install spawn-fcgi
sudo apt install libfcgi-dev
sudo ufw allow 7000

# origin also needs nginx running
sudo apt install nginx
# review server/nginx.conf file
# especially the paths

git clone https://github.com/sunnyszy/lrb-prototype ~/webtracereplay
cd ~/webtracereplay/origin
make
```


## on client
```shell script
sudo apt install libcurl4-gnutls-dev
git clone https://github.com/sunnyszy/lrb-prototype ~/webtracereplay
cd ~/webtracereplay/client
make
```