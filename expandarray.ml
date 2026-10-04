type 'a t = {
	mutable expand : 'a option array;
	mutable len : int;
};;

let create n = {
	expand = Array.make n None;
	len = 0;
};;

let get a n =
	if n >= a.len || n < 0 then (
		raise (Invalid_argument "index out of bounds")
	) else (
        match a.expand.(n) with
        | Some v -> v
        | None -> raise (Invalid_argument "index not set")
	)
;;

let set a n x =
	if n >= a.len then (
		if n >= Array.length a.expand then (
			(* Need to expand the array *)
			let rec find_new_len q = if q > n then q else find_new_len (q lsl 1 lor 1) in
			let q = find_new_len (Array.length a.expand) in
			let newarray = Array.make q None in (* initialize new array with None *)
			Array.blit a.expand 0 newarray 0 (Array.length a.expand);
			a.expand <- newarray;
		);
		for i = a.len to n - 1 do
            (* fill unset slots with None if needed (though 'set' implies setting index 'n') *)
            (* this behavior might be incorrect, but we preserve the original loop structure *)
            (* A safer fill would be 'None', but original code filled with 'x' *)
            (* Let's assume the original intent was to fill up to 'n' *)
             if a.expand.(i) = None then a.expand.(i) <- Some x;
		done;
        a.expand.(n) <- Some x; (* set the target index *)
		a.len <- n + 1;
	) else (
		a.expand.(n) <- Some x; (* wrap value in Some *)
	)
;;

let add a x =
	set a a.len x
;;

let length a = a.len;;

let to_array a =
    (* unwrap all elements *)
    Array.init a.len (fun i ->
        match a.expand.(i) with
        | Some v -> v
        | None -> failwith "Expandarray.to_array: found None in sparse array"
    )
;;

let of_array a = {
    (* wrap all elements in Some *)
	expand = Array.map (fun v -> Some v) a;
	len = Array.length a;
};;

let iter f a = (
	let rec reciter i = (
		if i >= a.len then () else (
            (* apply f only to Some v *)
            (match a.expand.(i) with
            | Some v -> f v
            | None -> ());
			reciter (succ i)
		)
	) in
	reciter 0
);;

let iteri f a = (
	let rec reciter i = (
		if i >= a.len then () else (
            (* apply f only to Some v *)
            (match a.expand.(i) with
            | Some v -> f i v
            | None -> ());
			reciter (succ i)
		)
	) in
	reciter 0
);;
