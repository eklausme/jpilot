%define version 0.97

Summary: palm pilot desktop for Linux
Name: jpilot
Version: %{version}
Release: 1
Copyright: GPL
Group: Applications/Communications
Source: http://jpilot.linuxbox.com/jpilot-%{version}.tar.gz
URL: http://jpilot.linuxbox.com
Packager: Judd Montgomery <judd@engineer.com>

%description
jpilot is a palm pilot desktop for Linux written by:
Judd Montgomery, judd@engineer.com

%prep
#This script was necessary for slackware, remove for RH
set -x

umask 022
cd /usr/src/rpm/BUILD
cd /usr/src/rpm/BUILD
rm -rf jpilot-0.97
/bin/gzip -dc /home/judd/jpilot-0.97.tar.gz | tar -xvvf -
STATUS=$?
if [ $STATUS -ne 0 ]; then
  exit $STATUS
fi
cd jpilot-0.97
[ `/usr/bin/id -u` = '0' ] && /bin/chown -Rf root .
#[ `/usr/bin/id -u` = '0' ] && /bin/chgrp -Rf root .
/bin/chmod -Rf a+rX,g-w,o-w .
exit 0

%setup
%build
./configure --prefix=/usr/
make
# Now do the plugin stuff
make libplugin
cd Expense
./configure --prefix=/usr/
make
%install
strip jpilot
install -m 555 -s jpilot -o root -g root /usr/bin/jpilot
install -m 755 -d /usr/share/jpilot/
install -m 755 -d /usr/share/jpilot/plugins/
install -m 644 jpilotrc.blue -o root -g root /usr/share/jpilot/jpilotrc.blue
install -m 644 jpilotrc.default -o root -g root /usr/share/jpilot/jpilotrc.default 
install -m 644 jpilotrc.green -o root -g root /usr/share/jpilot/jpilotrc.green
install -m 644 jpilotrc.purple -o root -g root /usr/share/jpilot/jpilotrc.purple
install -m 644 jpilotrc.steel -o root -g root /usr/share/jpilot/jpilotrc.steel
install -m 644 empty/DatebookDB.pdb -o root -g root /usr/share/jpilot/DatebookDB.pdb
install -m 644 empty/AddressDB.pdb -o root -g root /usr/share/jpilot/AddressDB.pdb
install -m 644 empty/ToDoDB.pdb -o root -g root /usr/share/jpilot/ToDoDB.pdb
install -m 644 empty/MemoDB.pdb -o root -g root /usr/share/jpilot/MemoDB.pdb
install -m 644 empty/MemoDB.pdb -o root -g root /usr/share/jpilot/MemoDB.pdb
install -m 644 Expense/libexpense.so -o root -g root /usr/share/jpilot/plugins/libexpense.so

%files
%attr(-, bin, bin) %doc BUGS CHANGELOG COPYING CREDITS INSTALL README TODO
%doc icons docs/plugin.html docs/manual.html
%attr(0555,root,root) /usr/bin/jpilot
%attr(0644,root,root) /usr/share/jpilot/jpilotrc.blue
%attr(0644,root,root) /usr/share/jpilot/jpilotrc.default
%attr(0644,root,root) /usr/share/jpilot/jpilotrc.green
%attr(0644,root,root) /usr/share/jpilot/jpilotrc.purple
%attr(0644,root,root) /usr/share/jpilot/jpilotrc.steel
%attr(0644,root,root) /usr/share/jpilot/DatebookDB.pdb
%attr(0644,root,root) /usr/share/jpilot/AddressDB.pdb
%attr(0644,root,root) /usr/share/jpilot/ToDoDB.pdb
%attr(0644,root,root) /usr/share/jpilot/MemoDB.pdb
%attr(0644,root,root) /usr/share/jpilot/plugins/libexpense.so
