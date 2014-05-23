Summary: A distributed web and enterprise search engine.
Name: gb
Version: 1.0
Release: 0
Copyright: GPL
Group: Applications/Databases
Source: https://github.com/gigablast/open-source-search-engine/archive/master.tar.gz
Patch:
BuildRoot: /var/tmp/%{name}-buildroot

%description
The gb program allows the user to index and search documents.

%prep
%setup -q
%patch -p1 -b .buildroot

%build
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS"

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/var/gigablast/data0/
./gb copyfiles $RPM_BUILD_ROOT/var/gigablast/data0/
ln -s $RPM_BUILD_ROOT/var/gigablast/data0/gb $RPM_BUILD_ROOT/usr/bin/
ln -s $RPM_BUILD_ROOT/lib/init/upstart-job $RPM_BUILD_ROOT/etc/init.d/gb
install -m 644 init.gb.conf $RPM_BUILD_ROOT/etc/init/gb.conf

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc README TODO COPYING ChangeLog

/usr/bin/gb

%changelog
* Thu May 22 2014 Matt Wells <gigablast@mail.com>
- Initial Package Release
