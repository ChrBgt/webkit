description(

"This test checks that null characters are allowed in JavaScript strings, rather than causing a parse error."

);

shouldBe('String(" ").length', "1");

successfullyParsed = true;
