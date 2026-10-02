# Decoded

`oa-ui-decoded` (`oa/ui/decoded.hpp`) is where a decoder's error becomes an
exception. The decoders in `src/formats` return `oa::base::bytes::Decoded`
values and never throw; the screens and the application, which treat a file
they cannot decode as fatal or catch the failure and skip the file, call
`require(decoded, what)`. It returns the value, or throws
`std::runtime_error` with `describe()`'s text: the file or entry, the
decoder's message, the error's code and its byte offset. The exception stays
in the layer that throws it.

Its test, `ui-decoded`, checks the value passed through and the text of the
exception.
