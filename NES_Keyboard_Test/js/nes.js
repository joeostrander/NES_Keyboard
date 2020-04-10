var elemBody = document.getElementsByTagName("body")[0];
elemBody.addEventListener("keyup", keyup);
elemBody.addEventListener("keydown", keydown);


var keyMap = {
              88:"divButtonA",
              90:"divButtonB",
              37:"divLeftArrow",
              38:"divUpArrow",
              39:"divRightArrow",
              40:"divDownArrow",
              16:"divSelect",
              13:"divStart"
              };

var intervalMs = 100; 
var maxMs = 1000;
var elapsedMs = 0;             
var myTimer;

function checkTimeout() {
  elapsedMs += intervalMs;
  
  if (elapsedMs >= maxMs) {
    clearInterval(myTimer);
    //set all buttons to normal class... remove .buttonPressed
    //  maybe just use jquery
    $(".buttonPressed").removeClass("buttonPressed");
    
    elapsedMs = 0;
  }
}


function keyup(event) {
  var char = event.which || event.keyCode;  // Use either which or keyCode, depending on browser support

  console.log("keyup keycode: "+char);
  //console.log("keyup key: "+String.fromCharCode(char));
  var myKey = keyMap[char];
  if (typeof myKey !== "undefined") {
    console.log("keyup " + keyMap[char]);
    //elemDivStatus.innerHTML = keyMap[char];
    $("#"+keyMap[char]).removeClass("buttonPressed");
  }
//  clearInterval(myTimer);
//  myTimer = setInterval(checkTimeout,intervalMs);
}

function keydown(event) {
  var char = event.which || event.keyCode;  // Use either which or keyCode, depending on browser support

  console.log("keydown keycode: "+char);
  //console.log("keydown key: "+String.fromCharCode(char));
  var myKey = keyMap[char];
  if (typeof myKey !== "undefined") {
    console.log("keydown " + keyMap[char]);
    $("#"+keyMap[char]).addClass("buttonPressed");
  }
//  clearInterval(myTimer);
//  myTimer = setInterval(checkTimeout,intervalMs);
}