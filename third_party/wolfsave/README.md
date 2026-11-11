WolfSave headers, MIT license, copyright 2024 Sinflower.
Upstream: https://github.com/Sinflower/WolfSave
Revision: 537ceba4262368c61bfb9c4fa5333341a115c662

Used only by the optional Windows PC-save converter, never linked into iOS.
Local changes: portable wide-path stream constructors and function-name error
strings; whitespace cleanup; an in-memory JSON accessor and read-only parser entry point. The
converter reads the user's local files and does not upload them.
