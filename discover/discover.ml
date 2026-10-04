module C = Configurator.V1

let () =
 C.main ~name:"discover_sse_flags" (fun c ->
  let architecture = C.ocaml_config_var_exn c "architecture" in

  let sse_flags =
   match architecture with
   | "x86_64" | "amd64" | "i686" ->
    ["-mssse3"; "-msse4.1"]
   | _ ->
    []
  in

  C.Flags.write_sexp "flags.sexp" sse_flags
 )