description("Series of tests to ensure correct behaviour on an invalid fillStyle()");
var ctx = document.getElementById('canvas').getContext('2d');
ctx.fillStyle = 'rgb(0, 255, 0)';
ctx.fillStyle = 'nonsense';
ctx.fillRect(0, 0, 200, 200);
var imageData = ctx.getImageData(0, 0, 200, 200);
var imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "255");
shouldBe("imgdata[6]", "0");

var successfullyParsed = true;
