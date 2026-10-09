Huxley/XNU

Huxley/XNU (HXNU) is a fork of Apple’s XNU kernel, developed for Huxley, an independent, Darwin-derived Unix-like operating system.

Huxley aims to build a general-purpose Unix environment around XNU, with support for traditional Unix software, modern desktop environments, and hardware outside of Apple’s ecosystem.

The project is in its early stages. Development is currently focused on the kernel itself, with the rest of the operating system to follow as the kernel becomes more stable and independent.

About Huxley

Huxley is an operating system based on Darwin, the open-source foundation of macOS. Rather than attempting to recreate macOS, Huxley takes a different direction, combining XNU’s existing architecture with a more traditional Unix environment.

The long-term goal is to support software commonly found on BSD and Linux systems, including X11, Wayland, and desktop environments such as KDE Plasma.

Package management through pkgsrc and pkgin is also planned.

Huxley is not a Linux distribution, nor is it intended to be a replacement for macOS. It is a separate operating system built from Darwin’s foundations.

The Name

Huxley is named after Thomas Henry Huxley (1825–1895), an English biologist and one of the most prominent defenders of Charles Darwin’s theory of evolution.

Huxley earned the nickname “Darwin’s Bulldog” for his public defense of Darwin’s work.

Since the operating system descends from Darwin, the name seemed appropriate.

The Kernel

XNU is a hybrid kernel combining components of Mach, BSD, and I/O Kit. It serves as the kernel of Darwin and macOS.

Huxley/XNU retains this foundation while gradually introducing changes necessary for Huxley’s development.

Initial work will focus on stability, portability, hardware support, and reducing dependencies on Apple’s proprietary operating system components.

The intention is to build upon XNU rather than rewrite it unnecessarily.

Development

Huxley is still experimental and is not ready for general use.

For now, development is concentrated in this repository. Additional repositories for the base system, system utilities, and other components will be created as the project progresses.

There is no stable release or supported installation procedure yet.

Upstream

Huxley/XNU is derived from Apple’s open-source XNU kernel.

Upstream source code is available at:

https://github.com/apple-oss-distributions/xnu

Huxley is an independent project and is not affiliated with or endorsed by Apple.

License

Huxley/XNU retains the applicable licenses and copyright notices of the original XNU source code, including the Apple Public Source License 2.0.

Modifications and additional components may be subject to their respective licenses. Refer to the source files and accompanying license notices for details.
