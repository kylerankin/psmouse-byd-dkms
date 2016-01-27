Name:		psmouse-byd-dkms
Version:	0.1
Release:	1%{?dist}
Summary:	psmouse module with BYD protocol support

Group:		System Environment/Kernel
License:	GPL v2 only

Requires:	dkms

%define _builddir %(pwd)

%description
This package contains DKMS packaging of psmouse module with BYD protocol
included. It is useful for Librem laptops.

%prep

%build

%install
make install DESTDIR=%{buildroot}

%files
/usr/src/psmouse-byd-%{version}/

%post
dkms add -m psmouse -v byd-%{version} --rpm_safe_upgrade

%preun
dkms remove -m psmouse -v byd-%{version} --all --rpm_safe_upgrade

%posttrans
dkms build -m psmouse -v byd-%{version} && dkms -m psmouse -v byd-%{version} || true

%changelog

