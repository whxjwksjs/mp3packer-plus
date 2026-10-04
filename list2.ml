(*******************************************************************************
	This file is a part of mp3packer.

	mp3packer is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	mp3packer is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mp3packer; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*******************************************************************************)

exception Empty;;

type 'a node = {
	value : 'a;
	mutable next : 'a node option;
	mutable prev : 'a node option;
};;

(* head and tail are now options, to safely represent an empty list *)
type 'a t = {
	mutable length : int;
	mutable head : 'a node option;
	mutable tail : 'a node option
};;


(* create initializes head and tail to None (safe) *)
let create () = {
  length = 0;
  head = None;
  tail = None
}

let clear q =
	q.length <- 0;
	q.head <- None;
	q.tail <- None;
;;

let length q = q.length;;
let is_empty q = (q.length = 0);;

let append q x =
	q.length <- succ q.length;
    let a = { value = x; next = None; prev = q.tail } in
	match q.tail with
	| None -> (* List was empty *)
		q.head <- Some a;
		q.tail <- Some a
	| Some old_tail -> (* List not empty *)
		old_tail.next <- Some a;
		q.tail <- Some a
;;

let prepend q x =
	q.length <- succ q.length;
    let a = { value = x; next = q.head; prev = None } in
    match q.head with
	| None -> (* List was empty *)
		q.head <- Some a;
		q.tail <- Some a
	| Some old_head -> (* List not empty *)
		old_head.prev <- Some a;
		q.head <- Some a
;;

let iter f q =
    match q.head with
    | None -> ()
    | Some h ->
		let rec iter node =
			f node.value;
			match node.next with
			| None -> ()
			| Some x -> iter x
		in
		iter h
;;

let rev_iter f q =
    match q.tail with
    | None -> ()
    | Some t ->
		let rec iter node =
			f node.value;
			match node.prev with
			| None -> ()
			| Some x -> iter x
		in
		iter t
;;

let iteri f q =
    match q.head with
    | None -> ()
    | Some h ->
		let rec iter i node =
			f i node.value;
			match node.next with
			| None -> ()
			| Some x -> iter (succ i) x
		in
		iter 0 h
;;

let rev_iteri f q =
    match q.tail with
    | None -> ()
    | Some t ->
		let rec iter i node =
			f i node.value;
			match node.prev with
			| None -> ()
			| Some x -> iter (pred i) x
		in
		iter (q.length - 1) t
;;

let pop_last q =
    let old_tail = (match q.tail with None -> raise Empty | Some t -> t) in
	match old_tail.prev with
	| None -> ( (* It's the only element in the list *)
		clear q;
		old_tail.value
	)
	| Some new_tail -> (
		new_tail.next <- None;
		old_tail.prev <- None; (* I don't think this is necessary, but I'll do it anyway *)
		q.tail <- Some new_tail; (* must be Some *)
		q.length <- pred q.length;
		old_tail.value
	)
;;
let take_last = pop_last;;

(* I don't like exceptions! *)
let pop_last_perhaps q =
    match q.tail with
    | None -> None
    | Some old_tail ->
        q.length <- pred q.length;
        (match old_tail.prev with
        | None -> ( (* It's the only element in the list *)
            q.head <- None;
            q.tail <- None;
            Some old_tail.value
        )
        | Some new_tail -> (
            new_tail.next <- None;
            old_tail.prev <- None; (* I don't think this is necessary, but I'll do it anyway *)
            q.tail <- Some new_tail;
            Some old_tail.value
        )
    )
;;
let take_last_perhaps = pop_last_perhaps;;


let pop_first q =
    let old_head = (match q.head with None -> raise Empty | Some h -> h) in
	match old_head.next with
	| None -> (
		clear q;
		old_head.value
	)
	| Some new_head -> (
		new_head.prev <- None;
		old_head.next <- None;
		q.head <- Some new_head; (* must be Some *)
		q.length <- pred q.length;
		old_head.value
	)
;;
let take_first = pop_first;;


let pop_first_perhaps q =
    match q.head with
    | None -> None
    | Some old_head ->
        q.length <- pred q.length;
		(match old_head.next with
		| None -> (
			q.head <- None;
            q.tail <- None;
			Some old_head.value
		)
		| Some new_head -> (
			new_head.prev <- None;
			old_head.next <- None;
			q.head <- Some new_head;
			Some old_head.value
		)
    )
;;
let take_first_perhaps = pop_first_perhaps;;


let fold f accu q =
    match q.head with
    | None -> accu
    | Some h ->
		let rec fold accu node =
			let accu = f accu node.value in
			match node.next with
			| None -> accu
			| Some x -> fold accu x
		in
		fold accu h
;;
let rev_fold f accu q =
    match q.tail with
    | None -> accu
    | Some t ->
		let rec fold accu node =
			let accu = f accu node.value in
			match node.prev with
			| None -> accu
			| Some x -> fold accu x
		in
		fold accu t
;;

let nth q n =
	if q.length = 0 then raise Empty;
	let n = (if n >= 0 then n else q.length + n) in (* Make "nth q ~-1" point to the last element *)
	if n >= q.length || n < 0 then raise (Invalid_argument "index out of bounds");
    match q.head with
    | None -> raise Empty (* Should be covered by q.length = 0, but good for safety *)
    | Some h ->
        let rec test i node =
            if i = n then node.value else (
                match node.next with
                | None -> raise (Invalid_argument "index out of bounds")
                | Some x -> test (succ i) x
            )
        in
	    test 0 h
;;

let peek_first q =
    match q.head with None -> raise Empty | Some h -> h.value
;;
let head = peek_first;;

let peek_first_perhaps q =
    match q.head with None -> None | Some h -> Some h.value
;;
let head_perhaps = peek_first_perhaps;;

let peek_last q =
	match q.tail with None -> raise Empty | Some t -> t.value
;;
let tail = peek_last;;

let peek_last_perhaps q =
	match q.tail with None -> None | Some t -> Some t.value
;;
let tail_perhaps = peek_last_perhaps;;

(* To other kinds of data structures *)
let of_array a =
	let q = create () in
	Array.iter (fun x -> append q x) a;
	q
;;

let to_array q =
    match q.head with
    | None -> [||]
    | Some h ->
        let a = Array.make q.length h.value in
		iteri (fun i v -> a.(i) <- v) q;
		a
;;

let of_list l =
	let q = create () in
	List.iter (fun x -> append q x) l;
	q
;;

let to_list q =
    match q.tail with
    | None -> []
    | Some t ->
		let rec make_list node list_tail =
			match node.prev with
			| None -> node.value :: list_tail
			| Some x -> make_list x (node.value :: list_tail)
		in
		make_list t []
;;
