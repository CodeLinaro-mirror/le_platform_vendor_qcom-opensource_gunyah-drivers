# If kversion isn't defined on the rpmbuild line, define it here.
%{!?kversion: %define kversion %(uname -r)}

%{!?with_oot_debug: %define with_oot_debug 0}

%define kmod_name gunyah-drivers

%define debug_package %{nil}

%if %{with_oot_debug}
    %define kpackage kernel-automotive-debug
    %define kversion_with_debug %{kversion}+debug
%else
    %define kversion_with_debug %{kversion}
    %define kpackage kernel-automotive
%endif

Name: %{kmod_name}
Version: 1.0
Release:        1%{?dist}
Summary: gunyah kernel drivers

License: GPLv2
Source0: %{name}-%{version}.tar.gz

BuildRequires: kernel-automotive-devel-uname-r = %{kversion_with_debug}
Requires: %{kpackage}-core-uname-r = %{kversion_with_debug}


%description
This is a rpm contains gunyah out of tree kernel modules.

%prep
%setup -qn %{name}

%build
KSRC=%{_usrsrc}/kernels/%{kversion_with_debug}
make KERNEL_SRC=${KSRC} all

%post
depmod -a

%postun
depmod -a

%install
mkdir -p %{buildroot}/usr/include/uapi/linux/
install -m 755 include/uapi/linux/gunyah.h %{buildroot}/usr/include/uapi/linux/
install_mod_path=%{buildroot}/usr/lib/modules/%{kversion_with_debug}
mkdir -p ${install_mod_path}/extra/arch/arm64/gunyah/
install -m 644 arch/arm64/gunyah/gh_arm_drv.ko  ${install_mod_path}/extra/arch/arm64/gunyah/gh_arm_drv.ko
mkdir -p ${install_mod_path}/extra/drivers/virt/gunyah/
install -m 644 drivers/virt/gunyah/gh_dbl.ko  ${install_mod_path}/extra/drivers/virt/gunyah/gh_dbl.ko
install -m 644 drivers/virt/gunyah/gh_msgq.ko  ${install_mod_path}/extra/drivers/virt/gunyah/gh_msgq.ko
install -m 644 drivers/virt/gunyah/gh_rm_drv.ko  ${install_mod_path}/extra/drivers/virt/gunyah/gh_rm_drv.ko
install -m 644 drivers/virt/gunyah/gunyah.ko  ${install_mod_path}/extra/drivers/virt/gunyah/gunyah.ko
mkdir -p ${install_mod_path}/extra/drivers/tty/hvc/
install -m 644 drivers/tty/hvc/hvc_gunyah.ko  ${install_mod_path}/extra/drivers/tty/hvc/hvc_gunyah.ko

%{__install} -d %{buildroot}%{_sysconfdir}/modules-load.d/
%{__install} %{kmod_name}.conf %{buildroot}%{_sysconfdir}/modules-load.d/

%clean
rm -rf $RPM_BUILD_ROOT

%files
%define kernel_module_path /usr/lib/modules/%{kversion_with_debug}
%{kernel_module_path}/extra/arch/arm64/gunyah/gh_arm_drv.ko
%{kernel_module_path}/extra/drivers/virt/gunyah/gh_dbl.ko
%{kernel_module_path}/extra/drivers/virt/gunyah/gh_msgq.ko
%{kernel_module_path}/extra/drivers/virt/gunyah/gh_rm_drv.ko
%{kernel_module_path}/extra/drivers/virt/gunyah/gunyah.ko
%{kernel_module_path}/extra/drivers/tty/hvc/hvc_gunyah.ko

%{_includedir}/uapi/linux/gunyah.h
%{_sysconfdir}/modules-load.d/%{kmod_name}.conf

%changelog
* Fri Sep 01 2023 Yimin Peng <quic_yiminp@quicinc.com> 1.0
- First commit!
