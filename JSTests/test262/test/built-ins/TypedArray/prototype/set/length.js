// Copyright (C) 2015 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es6id: 22.2.3.22
description: >
  %TypedArray%.prototype.set.length is 1.
info: |
  %TypedArray%.prototype.set ( overloaded [ , offset ])

  17 ECMAScript Standard Built-in Objects:
    Every built-in Function object, including constructors, has a length
    property whose value is an integer. Unless otherwise specified, this
    value is equal to the largest number of named arguments shown in the
    subclause headings for the function description, including optional
    parameters. However, rest parameters shown using the form “...name”
    are not included in the default argument count.

    Unless otherwise specified, the length property of a built-in Function
    object has the attributes { [[Writable]]: false, [[Enumerable]]: false,
    [[Configurable]]: true }.
includes: [propertyHelper.js, testTypedArray.js]
---*/

assert.sameValue(TypedArray.prototype.set.length, 1);

verifyNotEnumerable(TypedArray.prototype.set, "length");
verifyNotWritable(TypedArray.prototype.set, "length");
verifyConfigurable(TypedArray.prototype.set, "length");
