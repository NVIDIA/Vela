# Distinguish Archives, foreign conversion, and application dumps

VSR calls a native serialized representation an Archive, whether it is nested,
transmitted, or saved as a `.vsr` file. Runtime state is serialized to and
deserialized from Archive trees; Archives and Application Dumps are saved to
and loaded from files; byte buffers are written and read; and import/export is
reserved for conversion between VSR and non-native formats. `vsr_io` owns
Archives while `vsr_app` composes Scene and Animation Manager Archives with
application state into Application Dumps. This vocabulary makes the format
boundary explicit and prevents native persistence from being confused with
foreign-format conversion.
