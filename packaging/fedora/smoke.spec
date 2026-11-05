Name:           smoke
Version:        1.1.0
Release:        1%{?dist}
Summary:        A simple and lightweight command-line note-taking application

License:        Custom
URL:            https://github.com/xieguaiwu/Smoking_Note
Source0:        %{url}/archive/v%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc
Requires:       glibc

%description
Smoke is a simple and lightweight command-line note-taking application 
written in C. It allows you to quickly add, list, and edit notes with 
automatic timestamps.

Features:
- Add notes with automatic timestamps
- List all saved notes with line numbers
- Edit existing notes
- Cross-platform (Linux and Windows)
- Persistent storage in local text file

%prep
%setup -q -n Smoking_Note-%{version}

%build
gcc %{optflags} smoke.c -o smoke

%install
rm -rf %{buildroot}
install -Dm755 smoke %{buildroot}%{_bindir}/smoke
install -Dm755 check_notes.sh %{buildroot}%{_bindir}/smoke-check-notes
install -Dm644 LICENSE %{buildroot}%{_defaultlicensedir}/%{name}/LICENSE
install -Dm644 README.md %{buildroot}%{_defaultdocdir}/%{name}/README.md

%files
%license LICENSE
%doc README.md
%{_bindir}/smoke
%{_bindir}/smoke-check-notes

%changelog
* Thu Nov 05 2026 xgw <xieguaiwu@163.com> - 1.1.0-1
- Notes may span several lines; the editor saves one note, not one per line
- Remove the fixed 1023-byte note limit and the 1000-note edit limit
- Write edits to a temporary file and rename, so a failed write cannot
  damage the notes file
- Return meaningful exit codes
- Add smoke-check-notes to inspect files written by older versions

* Tue Mar 17 2026 xgw <xieguaiwu@163.com> - 1.0.0-1
- Initial package
