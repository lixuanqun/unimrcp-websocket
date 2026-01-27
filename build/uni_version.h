/*
 * UniMRCP version information
 */

#ifndef UNI_VERSION_H
#define UNI_VERSION_H

/** Major version number */
#define UNI_MAJOR_VERSION   1
/** Minor version number */
#define UNI_MINOR_VERSION   8
/** Patch version number */
#define UNI_PATCH_VERSION   0

/** Full version number in string format */
#define UNI_VERSION_STRING  "1.8.0"

/** Full version string (version + revision) */
#define UNI_FULL_VERSION_STRING UNI_VERSION_STRING

/** Version string for CSV format (Windows resource files) */
#define UNI_VERSION_STRING_CSV 1,8,0

/** Copyright string */
#define UNI_COPYRIGHT "Copyright 2008-2026 Arsen Chaloyan"

/** License string */
#define UNI_LICENSE "Licensed under the Apache License, Version 2.0"

/** Check at compile time if the version is at least a certain level */
#define UNI_VERSION_AT_LEAST(major,minor,patch) \
    (((major) < UNI_MAJOR_VERSION) || \
     ((major) == UNI_MAJOR_VERSION && (minor) < UNI_MINOR_VERSION) || \
     ((major) == UNI_MAJOR_VERSION && (minor) == UNI_MINOR_VERSION && (patch) <= UNI_PATCH_VERSION))

#endif /* UNI_VERSION_H */
