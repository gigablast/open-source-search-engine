AutoReqProv: no
%global debug_package %{nil}

Summary: A distributed web and enterprise search engine.
Name: gb
Version: 1.0
Release: 0
Group: Applications/Databases
Source: ./gb-1.0.tar
License: Apache
BuildRoot: /var/tmp/%{name}-buildroot

%description
The gb program allows the user to index and search documents.

%prep
%setup -q

%build
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS"

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/var/gigablast/data0/
mkdir -p $RPM_BUILD_ROOT/usr/bin/
mkdir -p $RPM_BUILD_ROOT/lib/init/
mkdir -p $RPM_BUILD_ROOT/etc/init.d/
mkdir -p $RPM_BUILD_ROOT/etc/init/
$RPM_BUILD_ROOT/../../BUILD/gb-1.0/gb copyfiles $RPM_BUILD_ROOT/var/gigablast/data0/
ln -s ../../var/gigablast/data0/gb $RPM_BUILD_ROOT/usr/bin/
ln -s ../../lib/init/upstart-job $RPM_BUILD_ROOT/etc/init.d/gb
install -m 644 init.gb.conf $RPM_BUILD_ROOT/etc/init/gb.conf

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
/var/gigablast/
/etc/init.d/gb
/etc/init/gb.conf
/usr/bin/gb

%changelog
* Thu May 22 2014 Matt Wells <gigablast@mail.com>
- Initial Package Release
