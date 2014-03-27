# Changelog

This is the changelog for `libsoldout` project.

Please note that this is a user-centered condensed changelog; for an
exhaustive list of changes, please consult the repository.

I don't see the point of duplicating this information here, while on 
the other hand I have often missed a simple change list containing 
only what affect users of release versions.


----


## Changes in v1.2 ##

  * Name change to libsoldout

  * Bug fixes:
      + various issues with matching emphasis delimiter have been fixed
      + better compatibility with non-gcc compilers
      + GNU make now uses correct compiler options to generate PIC code


## Changes in v1.1 ##

  * New features:
      + new callbacks for document prolog and epilog
      + support of PHP-Markdown-like tables, through 3 new callbacks

  * Several bug fixes, most notably:
      + span-level entities are now correctly recognized in headers
      + empty ATX-styles header (i.e. lines containing only `#` charcters)
        no longer crash the library
      + empty documents are now correctly handled
      + blackslash escapes work in inline URIs (allowing use of `)` characters 
        in URIs)
