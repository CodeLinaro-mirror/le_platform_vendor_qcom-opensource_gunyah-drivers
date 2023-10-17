# If kversion isn't defined on the rpmbuild line, define it here.
%{!?kversion: %define kversion %(uname -r)}

%define kmod_name gunyah-drivers

%define debug_package %{nil}

Name: %{kmod_name}
Version: 1.0
Release:        1%{?dist}
Summary: gunyah kernel drivers

License: GPLv2
Source0: %{name}-%{version}.tar.gz

BuildRequires: kernel-automotive-devel-uname-r = %{kversion}
Requires: kernel-automotive-core-uname-r = %{kversion}

%description
This is a rpm contains gunyah out of tree kernel modules.

%prep
%setup -qn %{name}

%build
KSRC=%{_usrsrc}/kernels/%{kversion}
make KERNEL_SRC=${KSRC} all

%post
depmod -a

%postun
depmod -a

%install
mkdir -p %{buildroot}/usr/include/uapi/linux/
install -m 755 include/uapi/linux/gunyah.h %{buildroot}/usr/include/uapi/linux/
mkdir -p %{buildroot}/usr/lib/modules/%{kversion}/extra/arch/arm64/gunyah/
install -m 644 arch/arm64/gunyah/gh_arm_drv.ko  %{buildroot}/usr/lib/modules/%{kversion}/extra/arch/arm64/gunyah/gh_arm_drv.ko
mkdir -p %{buildroot}/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/
install -m 644 drivers/virt/gunyah/gh_dbl.ko  %{buildroot}/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gh_dbl.ko
install -m 644 drivers/virt/gunyah/gh_msgq.ko  %{buildroot}/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gh_msgq.ko
install -m 644 drivers/virt/gunyah/gh_rm_drv.ko  %{buildroot}/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gh_rm_drv.ko
install -m 644 drivers/virt/gunyah/gunyah.ko  %{buildroot}/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gunyah.ko
mkdir -p %{buildroot}/usr/lib/modules/%{kversion}/extra/drivers/tty/hvc/
install -m 644 drivers/tty/hvc/hvc_gunyah.ko  %{buildroot}/usr/lib/modules/%{kversion}/extra/drivers/tty/hvc/hvc_gunyah.ko
%{__install} -d %{buildroot}%{_sysconfdir}/modules-load.d/
%{__install} %{kmod_name}.conf %{buildroot}%{_sysconfdir}/modules-load.d/

%clean
rm -rf $RPM_BUILD_ROOT

%files
/usr/lib/modules/%{kversion}/extra/arch/arm64/gunyah/gh_arm_drv.ko
/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gh_dbl.ko
/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gh_msgq.ko
/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gh_rm_drv.ko
/usr/lib/modules/%{kversion}/extra/drivers/virt/gunyah/gunyah.ko
/usr/lib/modules/%{kversion}/extra/drivers/tty/hvc/hvc_gunyah.ko
%{_includedir}/uapi/linux/gunyah.h
%{_sysconfdir}/modules-load.d/%{kmod_name}.conf

%changelog
* Fri Sep 01 2023 Yimin Peng <quic_yiminp@quicinc.com> 1.0
- First commit!
