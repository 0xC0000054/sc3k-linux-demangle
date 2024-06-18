# sc3k-linux-demangle

A utility that removes the name mangling from the debug symbol function names in the SimCity 3000 Unlimited
Linux release.

This program wraps the demangling code that shipped with the C++ compiler used to build the game (egcs-1.1.2).

## Usage

It is a command line application that takes 1 text file with the mangled function names as input. The function
names must each be on their own line.
The output parameter is optional, when it is omitted the input file will be overwritten.

`SC3KLinuxDemangle input.txt output.txt`

## License

This project is licensed under the terms of the GNU General Public License version 3.0.   
See [License.txt](License.txt) for more information.

## 3rd Party Code

[egcs-1.1.2](https://gcc.gnu.org/pub/gcc/releases/egcs-1.1.2/) - GNU General Public License version 2.0 or later.