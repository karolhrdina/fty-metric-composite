#
#    composite-metrics - Agent that computes new metrics from bunch of other metrics
#
#    Copyright (c) the Authors
#

Name:           composite-metrics
Version:        0.1.0
Release:        1
Summary:        agent that computes new metrics from bunch of other metrics
License:        MIT
URL:            http://example.com/
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  lua-devel
BuildRequires:  cxxtools-devel
BuildRequires:  libbiosproto-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
composite-metrics agent that computes new metrics from bunch of other metrics.


%prep
%setup -q

%build
sh autogen.sh
%{configure}
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%doc README.md COPYING
%{_bindir}/composite_metrics

%changelog
