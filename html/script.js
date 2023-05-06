var mode = 'null';
//var gpio_list = [0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16];
var wsQueue = [];
var wsBusy = false;
var wsTimerId;
var devices =[]; // Start with Empty Array
var wnrfTimerId;
var rfc_cache=0;
var rff_cache=0;


const auditInterval = setInterval(auditDevices, 5000);
// json with effect definitions
var effectInfo;

// Default modal properties
$.fn.modal.Constructor.DEFAULTS.backdrop = 'static';
$.fn.modal.Constructor.DEFAULTS.keyboard = false;

var admin_ctl=false;

// Histogram
histData=[];
histPhase = 0;

for (i=0;i<84;i++) {
   histData.push(0);
}

// jQuery doc ready
$(function() {
    // Menu navigation for single page layout
    $('ul.navbar-nav li a').click(function() {
        // Highlight proper navbar item
        $('.nav li').removeClass('active');
        $(this).parent().addClass('active');

        // Show the proper menu div
        $('.mdiv').addClass('hidden');
        $($(this).attr('href')).removeClass('hidden');

        // kick start the live stream
        if ($(this).attr('href') == "#diag") {
            wsEnqueue('V1');
        }

        // kick start the frequency scanner
        if ($(this).attr('href') == "#hist") {
            wsEnqueue('V2');
        }

        // Collapse the menu on smaller screens
        $('#navbar').removeClass('in').attr('aria-expanded', 'false');
        $('.navbar-toggle').attr('aria-expanded', 'false');

        // Firmware selection and upload
        $('#efu').change(function () {
            $('#update').modal();
            $('#updatefw').submit();
        });

        // Hex file selection and upload
        $('#hex').change(function () {
            $('#hexup').modal();
            $('#wnrfu').submit();
            // Wait for server response (or timeout) to clear the modal
 // Add a timeout handler here.. *OR* can we probe the hidden frame to
 // see it's status?
          });

        $('#crfc').change(function() {
          // Warn Administrator
          if (window.confirm("Move the device to a different nRF Universe")) {
             $('#rfcommit').removeClass('hidden');
          } else {
             $('#crfc').val(rfc_cache);
             if ($('#crfr').val()==rfr_cache) 
                $('#rfcommit').addClass('hidden');
          }
        });

        $('#crfr').change(function() {
          // Warn Administrator
          if (window.confirm("Change to different Transmission Rate?")) {
             $('#rfcommit').removeClass('hidden');
          } else {
             $('#crfr').val(rfr_cache);
             if ($('#crfc').val()==rfc_cache) 
                $('#rfcommit').addClass('hidden');
          }
        });

        $('#newid').change(function() {
           var regexp = new RegExp(/^[0-9A-F]{6}$/i);
           if (!regexp.test($('#newid').val()))  {
              alert('Invalid DeviceId: '+$('#newid').val());
           } else {
              if (window.confirm("Confirm change to deviceID?")) {
                 var json = {
                    'devid': $('#ed_devid').text(),
                    'newid': $('#newid').val()
                     };
                 $('#update').modal();
                 wsEnqueue('D5' + JSON.stringify(json))
              } else {
                // Restore the device id
                $('#newid').val($('#ed_devid').text());
              }
           }
          });

        // Color Picker
        $('.color').colorPicker({
            buildCallback: function($elm) {
                var colorInstance = this.color;
                var colorPicker = this;

                $elm.append('<div class="cp-memory">' +
                    '<div style="background-color: #FFFFFF";></div>' +
                    '<div style="background-color: #FF0000";></div>' +
                    '<div style="background-color: #00FF00";></div>' +
                    '<div style="background-color: #0000FF";></div>').
                on('click', '.cp-memory div', function(e) {
                    var $this = $(this);

                    if (this.className) {
                        $this.parent().prepend($this.prev()).children().eq(0).
                            css('background-color', '#' + colorInstance.colors.HEX);
                    } else {
                        colorInstance.setColor($this.css('background-color'));
                        colorPicker.render();
                    }
                });

                this.$colorPatch = $elm.prepend('<div class="cp-disp">').find('.cp-disp');
            },

            cssAddon:
                '.cp-memory {margin-bottom:6px; clear:both;}' +
                '.cp-memory div {float:left; width:25%; height:40px;' +
                'background:rgba(0,0,0,1); text-align:center; line-height:40px;}' +
                '.cp-disp{padding:10px; margin-bottom:6px; font-size:19px; height:40px; line-height:20px}' +
                '.cp-xy-slider{width:200px; height:200px;}' +
                '.cp-xy-cursor{width:16px; height:16px; border-width:2px; margin:-8px}' +
                '.cp-z-slider{height:200px; width:40px;}' +
                '.cp-z-cursor{border-width:8px; margin-top:-8px;}',

            opacity: false,

            renderCallback: function($elm, toggled) {
                var colors = this.color.colors.RND;
                var json = {
                        'r': colors.rgb.r,
                        'g': colors.rgb.g,
                        'b': colors.rgb.b
                    };

                this.$colorPatch.css({
                    backgroundColor: '#' + colors.HEX,
                    color: colors.RGBLuminance > 0.22 ? '#222' : '#ddd'
                }).text(this.color.toString($elm._colorMode)); // $elm.val();

                var tmode = $('#tmode option:selected').val();
                if (typeof effectInfo[tmode].wsTCode !== 'undefined') {
                    if (effectInfo[tmode].hasColor) {
                        wsEnqueue( effectInfo[tmode].wsTCode + JSON.stringify(json) );
                    }
                }
            }
        });

        // Set page event feeds
        feed();
    });

    // Reverse checkbox
    $('.reverse').click(function() {
      // On click(), the new checkbox state has already been set
      var json = { 'reverse': $(this).prop('checked') };
      var tmode = $('#tmode option:selected').val();

      if (typeof effectInfo[tmode].wsTCode !== 'undefined') {
          if (effectInfo[tmode].hasReverse) {
              wsEnqueue( effectInfo[tmode].wsTCode + JSON.stringify(json) );
          }
      }
    });

    // Mirror checkbox
    $('.mirror').click(function() {
      // On click(), the new checkbox state has already been set
      var json = { 'mirror': $(this).prop('checked') };
      var tmode = $('#tmode option:selected').val();

      if (typeof effectInfo[tmode].wsTCode !== 'undefined') {
          if (effectInfo[tmode].hasMirror) {
              wsEnqueue( effectInfo[tmode].wsTCode + JSON.stringify(json) );
          }
      }
    });

    // AllLeds checkbox
    $('.allleds').click(function() {
      // On click(), the new checkbox state has already been set
      var json = { 'allleds': $(this).prop('checked') };
      var tmode = $('#tmode option:selected').val();

      if (typeof effectInfo[tmode].wsTCode !== 'undefined') {
          if (effectInfo[tmode].hasAllLeds) {
              wsEnqueue( effectInfo[tmode].wsTCode + JSON.stringify(json) );
          }
      }
    });

    // Effect speed field
    $('#t_speed').change(function() {
      var json = { 'speed': $(this).val() };
      var tmode = $('#tmode option:selected').val();

      if (typeof effectInfo[tmode].wsTCode !== 'undefined') {
          wsEnqueue( effectInfo[tmode].wsTCode + JSON.stringify(json) );
      }
    });

    // Effect brightness field
    $('#t_brightness').change(function() {
      var json = { 'brightness': $(this).val() };
      var tmode = $('#tmode option:selected').val();

      if (typeof effectInfo[tmode].wsTCode !== 'undefined') {
          wsEnqueue( effectInfo[tmode].wsTCode + JSON.stringify(json) );
      }
    });

    // Test mode toggles
    $('#tmode').change(hideShowTestSections());

    // DHCP field toggles
    $('#dhcp').click(function() {
        if ($(this).is(':checked')) {
            $('.dhcp').addClass('hidden');
       } else {
            $('.dhcp').removeClass('hidden');
       }
    });

    $('#nrf_legacy').click(function() {
        if ($(this).is(':checked')) {
            $('.nrf').addClass('hidden');
            $('.devadmin').addClass('hidden');
            $('.devchk').addClass('hidden');
       } else {
            $('.nrf').removeClass('hidden');
            $('.devchk').removeClass('hidden');
            if (document.getElementById('dev_admin').checked == true) {
               $('.devadmin').removeClass('hidden');
            }
       }
    });

    $('#dev_admin').click(function() { if ($(this).is(':checked')) {
            if (window.confirm("Device Admin will pause data streaming..")) {
               $('.devadmin').removeClass('hidden');
               wsEnqueue('DA'); // Enabled ADMIN mode
            } else {
               document.getElementById('dev_admin').checked = false;
            }
       } else {
            $('.devadmin').addClass('hidden');
            $('.devedit').addClass('hidden'); // includes bledit and apedit
            wsEnqueue('Da'); // Disable ADMIN mode
            clearInterval(wnrfTimerId);
       }
    });



    // WNRF OTA enable toggles
    $('#ota').click(function() {
      var json = {
          'devid': $('#ed_devid').text()
       };
       wsEnqueue('D4' + JSON.stringify(json));
       $('#update').modal();
    });

    // WNRF CLIENT RF UPDATE
    $('#rfcommit').click(function() {
      if (window.confirm("Confirm changes to RF settings?")) {
         var json = {
             'devid': $('#ed_devid').text(),
             'rfchan': $('#crfc').val(),
             'rfrate': $('#crfr').val()
          };
          wsEnqueue('D6' + JSON.stringify(json));
          $('#update').modal();
       } else {
          // Do Nothing
       }
    });


    // Hostname, SSID, and Password validation
    $('#hostname').keyup(function() {
        wifiValidation();
    });
    $('#staTimeout').keyup(function() {
        wifiValidation();
    });
    $('#ssid').keyup(function() {
        wifiValidation();
    });
    $('#password').keyup(function() {
        wifiValidation();
    });
    $('#ap').change(function () {
        wifiValidation();
    });
    $('#dhcp').change(function () {
        wifiValidation();
    });
    $('#gateway').keyup(function () {
        wifiValidation();
    });
    $('#ip').keyup(function () {
        wifiValidation();
    });
    $('#netmask').keyup(function () {
        wifiValidation();
    });

    canvas = document.getElementById("canvas");
    ctx = canvas.getContext("2d");
    ctx.font = "20px Arial";
    ctx.textAlign = "center";

    canvas2 = document.getElementById("canvas2");
    ctx2 = canvas2.getContext("2d");
    ctx2.font = "20px Arial";
    ctx2.textAlign = "center";

    // autoload tab based on URL hash
    var hash = window.location.hash;
    hash && $('ul.navbar-nav li a[href="' + hash + '"]').click();

});

function wifiValidation() {
    var WifiSaveDisabled = false;
    var re = /^([a-zA-Z0-9][a-zA-Z0-9][a-zA-Z0-9\-.]*[a-zA-Z0-9.])$/;
    if (re.test($('#hostname').val()) && $('#hostname').val().length <= 255) {
        $('#fg_hostname').removeClass('has-error');
        $('#fg_hostname').addClass('has-success');
    } else {
        $('#fg_hostname').removeClass('has-success');
        $('#fg_hostname').addClass('has-error');
        WifiSaveDisabled = true;
    }
    if ($('#staTimeout').val() >= 5) {
        $('#fg_staTimeout').removeClass('has-error');
        $('#fg_staTimeout').addClass('has-success');
    } else {
        $('#fg_staTimeout').removeClass('has-success');
        $('#fg_staTimeout').addClass('has-error');
        WifiSaveDisabled = true;
    }
    if ($('#ssid').val().length <= 32) {
        $('#fg_ssid').removeClass('has-error');
        $('#fg_ssid').addClass('has-success');
    } else {
        $('#fg_ssid').removeClass('has-success');
        $('#fg_ssid').addClass('has-error');
        WifiSaveDisabled = true;
    }
    if ($('#password').val().length <= 64) {
        $('#fg_password').removeClass('has-error');
        $('#fg_password').addClass('has-success');
    } else {
        $('#fg_password').removeClass('has-success');
        $('#fg_password').addClass('has-error');
        WifiSaveDisabled = true;
    }
    if ($('#dhcp').prop('checked') === false) {
        var iptest = new RegExp(''
        + /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\./.source
        + /(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\./.source
        + /(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\./.source
        + /(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/.source
        );

        if (iptest.test($('#ip').val())) {
            $('#fg_ip').removeClass('has-error');
            $('#fg_ip').addClass('has-success');
        } else {
            $('#fg_ip').removeClass('has-success');
            $('#fg_ip').addClass('has-error');
            WifiSaveDisabled = true;
        }
        if (iptest.test($('#netmask').val())) {
            $('#fg_netmask').removeClass('has-error');
            $('#fg_netmask').addClass('has-success');
        } else {
            $('#fg_netmask').removeClass('has-success');
            $('#fg_netmask').addClass('has-error');
            WifiSaveDisabled = true;
        }
        if (iptest.test($('#gateway').val())) {
            $('#fg_gateway').removeClass('has-error');
            $('#fg_gateway').addClass('has-success');
        } else {
            $('#fg_gateway').removeClass('has-success');
            $('#fg_gateway').addClass('has-error');
            WifiSaveDisabled = true;
        }
    }
    $('#btn_wifi').prop('disabled', WifiSaveDisabled);
}

function rxNewidReply(data) {
    var admin = JSON.parse(data);

    if (admin.result != 1) {
       admin_ctl=false;
       footermsg("WNRF: Failure to set new device address");
    } else {
       // Search for a match and delete it. 
       // Table will be repopulated on next BEACON update
       var devindex =-1;
       for (var i=0;i<devices.length;i++) {
          if (devices[i].dev_id == admin.dev_id)  {
             devices.splice(i,1); //deleteRow(i);
             break;
          }
       }
       $('#ed_devid').text($('#newid').val());
       showDevices();
    }
    $('#update').modal('hide');
}

function rxNewChanReply(data) {
    var admin = JSON.parse(data);

    if (admin.result != 1) {
       admin_ctl=false;
       footermsg("WNRF: Failure to set new channel address");
    } else {
       // Search the table looking for the device that matches
       // Need to update the DATA vs the HTML table, as a redraw
       // will pull from the DATA. 
       var devindex =-1;
       for (var i=0;i<devices.length;i++) {
          if (devices[i].dev_id == admin.dev_id)  {
             devindex=i;
             break;
          }
       }

       if (devindex==-1) {
         footermsg('Timeout or error from Device: re-check result');
       } else {
          devices[devindex].start = $('#s_chanid').val();
       }
       showDevices();

    }
    $('#update').modal('hide');
}

// Page event feeds
function feed() {
    if ($('#home').is(':visible')) {
        wsEnqueue('XJ');

        setTimeout(function() {
            feed();
        }, 1000);
    }
}

function param(name) {
    return (location.search.split(name + '=')[1] || '').split('&')[0];
}

// WebSockets
function wsConnect() {
    if ('WebSocket' in window) {

// accept ?target=10.0.0.123 to make a WS connection to another device
        if (target = param('target')) {
//
        } else {
            target = document.location.host;
        }

        // Open a new web socket and set the binary type
        ws = new WebSocket('ws://' + target + '/ws');
        ws.binaryType = 'arraybuffer';

        ws.onopen = function() {
            $('#wserror').modal('hide');
            wsEnqueue('E1'); // Get html elements
            wsEnqueue('G1'); // Get Config
            wsEnqueue('G2'); // Get Net Status
            wsEnqueue('G3'); // Get Effect Info

            feed();
        };

        ws.onmessage = function (event) {
            if(typeof event.data === "string") {
                var cmd = event.data.substr(0, 2);
                var data = event.data.substr(2);
                switch (cmd) {
                case 'E1':
                    getElements(data);
                    break;
                case 'G1':
                    getConfig(data);
                    break;
                case 'G2':
                    getConfigStatus(data);
                    break;
                case 'G3':
                    getEffectInfo(data);
                    break;
                case 'DA':
                    rxAdminReplyDA(data);
                    break;
                case 'Da':
                    rxAdminReplyDa(data);
                    break;
                case 'D1':
                    getDevices(data);
                    break;
                case 'D2':
                    rxNewChanReply(data);
                    break;
                case 'D3':
                    rxWnrfuReply(data);
                    break;
                case 'D4':
                    rxOTAReply(data);
                    break;
                case 'D5':
                    rxNewidReply(data);
                    break;
                case 'S1':
                    setConfig(data);
                    reboot();
                    break;
                case 'S2':
                    setConfig(data);
                    break;
                case 'S3':
                    footermsg('Configuration Saved');
                    break;
                case 'S4':
                    break;
                case 'XJ':
                    getJsonStatus(data);
                    break;
                case 'X6':
                    showReboot();
                    break;
                default:
                    console.log('Unknown Command: ' + event.data);
                    break;
                }
            } else {
                streamData= new Uint8Array(event.data);

                if (streamData.length==84) {  // Major hack for now
                   drawHist(streamData);
                   if ($('#hist').is(':visible')) {
                      wsEnqueue('V2');
                   }
                } else {
                   drawStream(streamData);
                   if ($('#diag').is(':visible')) {
                      wsEnqueue('V1');
                   }
                }
            }
            wsReadyToSend();
        };

        ws.onclose = function() {
            $('#wserror').modal();
            wsConnect();
        };

    } else {
        alert('WebSockets is NOT supported by your Browser! You will need to upgrade your browser or.');
    }
}

function wsEnqueue(message) {
    //only add a message to the queue if there isn't already one of the same type already queued, otherwise update the message with the latest request.
    wsQueueIndex=wsQueue.findIndex(wsCheckQueue,message);
    if(wsQueueIndex == -1) {
        //add message
        wsQueue.push(message);
    } else {
        //update message
        wsQueue[wsQueueIndex]=message;
    }
    wsProcessQueue();
}

function wsCheckQueue(value) {
    //messages are of the same type if the first two characters match
    return value.substr(0,2)==this.substr(0,2);
}

function wsProcessQueue() {
    //check if currently waiting for a response
    if(wsBusy) {
        //console.log('WS queue busy : ' + wsQueue);
    } else {
        //set wsBusy flag that we are waiting for a response
        wsBusy=true;
        //get next message from queue.
        message=wsQueue.shift();
        //set timeout to clear flag and try next message if response isn't recieved. Short timeout for message types that don't generate a response.
        if(['T0','T1','T2','T3','T4','T5','T6','T7','X6'].indexOf(message.substr(0,2))) {
            timeout=40;
        } else {
            timeout=2000;
        }
        wsTimerId=setTimeout(wsReadyToSend,timeout);
        //send it.
        //console.log('WS sending ' + message);
        ws.send(message);
    }
}

function wsReadyToSend() {
    clearTimeout(wsTimerId);
    wsBusy=false;
    if(wsQueue.length>0) {
        //send next message
        wsProcessQueue();
    } else {
        //console.log('WS queue empty');
    }
}

function drawStream(streamData) {
    var cols=parseInt($('#v_columns').val());
    var size=Math.floor((canvas.width-20)/cols);
    if($("input[name='viewStyle'][value='RGB']").prop('checked')) {
        maxDisplay=Math.min(streamData.length, (cols*Math.floor((canvas.height-30)/size))*3);
        for (i = 0; i < maxDisplay; i+=3) {
            ctx.fillStyle='rgb(' + streamData[i+0] + ',' + streamData[i+1] + ',' + streamData[i+2] + ')';
            var col=(i/3)%cols;
            var row=Math.floor((i/3)/cols);
            ctx.fillRect(10+(col*size),10+(row*size),size-1,size-1);
        }
    } else {
        maxDisplay=Math.min(streamData.length, (cols*Math.floor((canvas.height-30)/size)));
        for (i = 0; i < maxDisplay; i++) {
            ctx.fillStyle='rgb(' + streamData[i] + ',' + streamData[i] + ',' + streamData[i] + ')';
            var col=(i)%cols;
            var row=Math.floor(i/cols);
            ctx.fillRect(10+(col*size),10+(row*size),size-2,size-2);
        }
    }
    if(streamData.length>maxDisplay) {
        ctx.fillStyle='rgb(204,0,0)';
        ctx.fillRect(0,canvas.height-25,canvas.width,25);
        ctx.fillStyle='rgb(255,255,255)';
        ctx.fillText("Increase number of columns to show all data" , (canvas.width/2), canvas.height-5);
    }

}

function drawHist(histStream) {
    if (typeof ctx2 !== 'undefined') {
       ctx2.fillStyle='rgb(0,200,180)';
       histPhase=(histPhase+1)%5;
       if (histPhase == 0 ) {
          ctx2.clearRect(0, 0, canvas2.width, canvas2.height);
          for (i = 0; i < 84; i++) {
             if (histData[i]>0){
                histData[i]--;
             }
          }
       }

       for (i = 0; i < 84; i++) {
           histData[i]+=histStream[i];
           if (histData[i]>9) {
              histData[i]=9;
           }
           if (i>11) {
              if (((i-12)%5)==0) {
                 ctx2.fillStyle='rgb(2000,200,0)';
              }
              if (((i-12)%5)==1) {
                 ctx2.fillStyle='rgb(0,200,180)';
              }
           }
           ctx2.fillRect(10+(i*5),250-(histData[i]*25+2),4,(histData[i]*25)+2);
           if (i%20==0) {
              ctx2.fillText(i,10+i*5, 270 );
           }
       }
       ctx2.fillStyle='rgb(2000,200,0)';
       for (i = 0; i < 11; i++) {
              ctx2.fillText(i+1,10+((i*5+12))*5, 290 );
       }
    }
}

function clearStream() {
    if (typeof ctx !== 'undefined') {
     ctx.clearRect(0, 0, canvas.width, canvas.height);
    }
}

function getElements(data) {
    var elements = JSON.parse(data);

    for (var i in elements) {
        for (var j in elements[i]) {
            var opt = document.createElement('option');
            opt.text = j;
            opt.value = elements[i][j]; 
            document.getElementById(i).add(opt);
        }
    }
}

function getpcb( pcb_id ) {
  switch(pcb_id) {
      case 0x00: return "DEV BOARD"; break;
      case 0x01: return "3CHAN"; break;
      case 0x02: return "NRF2DMX"; break;
      case 0x03: return "LUXEONRGB"; break;
      case 0x04: return "12CHAN"; break;
      case 0x05: return "ARDUINO UNO"; break;
      case 0x06: return "RFCOLOR"; break;
      default:
         return "N/A'"; break;
  }
}
function showDevices() {
    // Code to re-populated the HTML <TABLE>
    var table = document.getElementById("nrf_list");
    var rowCount = table.rows.length;

    // Update by deleting everything and then adding again
    // start at 1 to leave the table header
    for (var i=1;i<rowCount;i++) {
       table.deleteRow(1);
    }

    for (var i=0;i<devices.length;i++) {
           var row = table.insertRow(i+1); // Offset for header
           row.insertCell(0).innerHTML= devices[i].dev_id;
	   row.insertCell(1).innerHTML=getpcb(devices[i].pcb); 

           if (devices[i].apv == 0xFF) {
             row.insertCell(2).innerHTML= "N/A"
             row.insertCell(3).innerHTML= "---";
           } else {
             row.insertCell(2).innerHTML= ((devices[i].apv>>4)&0x0F)+'.'+(devices[i].apv&0x0F);
             row.insertCell(3).innerHTML= devices[i].start;
           }
           if (admin_ctl) {
              row.insertCell(4).innerHTML= "<input type=\"radio\" id=\""+devices[i].dev_id+"\" onClick=\"editDevice(\'"+devices[i].dev_id+"\');\">";
           }
    }
}

function auditDevices() { // a 5 second periodic
    // Walk the list.. decrement the audit counter, any more more than nn seconds - remove
    for (var i=0;i<devices.length;i++) {
       devices[i].audit--;
       if (devices[i].audit==0) {
          devices.splice(i,1);
          i--;  // Needed as the array is now 1 row shorter
       }
    }
    showDevices();
}

function getDevices(data) {
    var devlist = JSON.parse(data);

    for (var i in devlist) {
       // For each parsed device - see if it is already in the array
       var index = devices.findIndex(item => item.dev_id === i);
       if (index ==-1) {
          // This is a new array entry - add to the list
          devices.push({dev_id: i,
                        pcb:   devlist[i].pcb,
                        proc:  devlist[i].proc,
                        blv:   devlist[i].blv,
                        apm:   devlist[i].apm,
                        apv:   devlist[i].apv,
                        start: devlist[i].start+1,
                        numchan: devlist[i].numchan,
                        rfrate: devlist[i].rfrate,
                        rfchan: devlist[i].rfchan,
                        cap:    devlist[i].devcap,
                        audit: 6});
        } else {
           devices[index].audit=6; // Renew it's lease
           devices[index].apm = devlist[i].apm;
           devices[index].apv = devlist[i].apv;
           devices[index].start = devlist[i].start+1;
        }
    }
}

function rxAdminReplyDA(data) {
    var admin = JSON.parse(data);

    if (admin.result == false) {
       admin_ctl=false;
       footermsg("WNRF: Error processing request (likely second user)");
    } else {
       admin_ctl = true;
       var x = document.getElementById('stat_mode');
       x.style.color="RED";
       x = document.getElementById('status');
       x.style.color='red';
       $('#stat_mode').text("ADMIN");
    }
}

function rxAdminReplyDa(data) {
    var admin = JSON.parse(data);

    if (admin.result == false) {
       footermsg("WNRF: Error processing request (likely second user)");
    }
    var x = document.getElementById('stat_mode');
    x.style.color="BLACK";
    $('#stat_mode').text("BROADCAST");
    x = document.getElementById('status');
    x.style.color="#9d9ded";
    admin_ctl=false;
}

function rxOTAReply(data) {
    var ota = JSON.parse(data);

    if (ota.result==1) {
       // Delete device from the list - let refresh pickup the new values
       var devindex =-1;
       for (var i=0;i<devices.length;i++) {
          if (devices[i].dev_id == ota.dev_id)  {
             devices.splice(i,1);
             break;
          }
       }
    } else {
       footermsg("OTA: Error on file upload ["+ota.result+"]");
    }
    $('#update').modal('hide');
}

function rxWnrfuReply(data) {
    var wnrfu = JSON.parse(data);

    if (wnrfu.retcode == 200) {
       $('#nrf_fw').text(wnrfu.nrf_fw);
       $('#hexup').modal('hide');
       $('#ota').prop('disabled',false);
    } else {
       footermsg("WNRF: Error on file upload ["+wnrfu.retcode+"]");
       $('#ota').prop('disabled',true);
    }
}

function editDevice(devid) {
     // Click on Radio to close edit
     if ($('#ed_devid').text() == devid) {
        doneEdit();
        return;
     }

    // Search the table looking for the device that matches
    var devindex =-1;
    for (var i=0;i<devices.length;i++) {
       if (devices[i].dev_id == devid)  {
          devindex=i;
          break;
       }
    }

   if (devindex==-1) {
      footermsg('Device no longer in range');
      return;
   }

   //Pulling from Table vs asking server to confirm with device
   // Search table for row that matches

     var device = devices[devindex];

     $('#ed_devid').text(device.dev_id);
     $('#ed_type').text(getpcb(device.pcb)+'   ('+device.apm+')'); 

     switch (device.proc) {
        case 0x00: $('#ed_proc').text('16F1823'); break;
        case 0x01: $('#ed_proc').text('16F1825'); break;
        case 0x02: $('#ed_proc').text('16F722'); break;
        case 0x80: $('#ed_proc').text('ATMEGA328P'); break;
        default: $('#ed_proc').text('UNKNOWN'); break;
     }

     $('#ed_numc').text(device.numchan);
     $('#ed_blv').text(((device.blv>>4)&0x0F)+'.'+((device.blv&0x0F)));
     $('#ed_apv').text( ((device.apv>>4)&0x0F)+'.'+((device.apv&0x0F)));

     // Based on capabilities - show data, or show form
     if (device.cap & 0x18) {
        console.log("CAPABILITY: "+device.cap);
        // SELECT entries for RFC and/or RFR
        rfc_cache = device.rfchan;
        rfr_cache = device.rfrate;
        for (i=70;i<84;i+=2) {
            var opt = document.createElement('option');
               if (i==80) 
                  opt.text ="Legacy ("+(2400+i)+")";
               else
                  opt.text = 2400+i;

               opt.value = i;
               if (device.rfchan==i) 
                 opt.selected = true;

               document.getElementById('crfc').add(opt);
        }
        for (i=1;i<3;i++) {
           var opt = document.createElement('option');
           opt.text = i+" Mbps";
           opt.value = i;
           if (device.rfrate==i) 
              opt.selected = true;
           document.getElementById('crfr').add(opt);
        }
        $('#rfcommit').addClass('hidden'); // hide until change
     } else { 
        $('#ed_rfc').text( device.rfchan );
        $('#ed_rfr').text( device.rfrate );
        $('#rfcommit').addClass('hidden');
        $('#crfc').addClass('hidden');
        $('#crfr').addClass('hidden');
     }

     $('#s_chanid').val(device.start); // The RANGE slider
     $('#channel').val(device.start);  // The RANGE output

     $('#newid').val(device.dev_id); // Device Id

     // Enable AP editing if AP version is not 0xFF
     if (device.apv != 0xFF) {
        $('.apedit').removeClass('hidden');
     } else {
        $('.apedit').addClass('hidden');
     }

     // Enabled the OTA update if ADMIN and HEX file is present
     if (admin_ctl) {
        $('#ota').prop('disabled',($('#nrf_fw').text() === 'none'));

        // Only show if APM indicates OTA capability
   // TODO: Change to show if FIRMWARE MAGIC = APM (compatability check)
        if (device.apm > 0) {
           $('#ota').removeClass('hidden');
        } else {
           $('#ota').addClass('hidden');
        }
     }

     // Display the Edit Panes
     $('.devedit').removeClass('hidden');

     document.getElementById(device.dev_id).checked = false;
}

function editChan() {
    var json = {
           'devid': $('#ed_devid').text(),
           'chan' : $('#s_chanid').val()
        };
    $('#update').modal();
    wsEnqueue('D2' + JSON.stringify(json))
}

function doneEdit() {
    // Update the table ?
     ($('#ed_devid').text(""));
     $('.devedit').addClass('hidden');

    //Clean up the dynamic SELECT boxes
    var x = document.getElementById('crfr');
    while (x.options.length >0) 
        x.remove(0);

    var x = document.getElementById('crfc');
    while (x.options.length >0) 
        x.remove(0);

    
}

function getConfig(data) {
    var config = JSON.parse(data);

    // Device and Network config
    $('#title').text('ESPS - ' + config.device.id);
    $('#name').text(config.device.id);
    $('#devid').val(config.device.id);
    $('#ssid').val(config.network.ssid);
    $('#password').val(config.network.passphrase);
    $('#hostname').val(config.network.hostname);
    $('#staTimeout').val(config.network.sta_timeout);
    $('#dhcp').prop('checked', config.network.dhcp);
    if (config.network.dhcp) {
        $('.dhcp').addClass('hidden');
    } else {
        $('.dhcp').removeClass('hidden');
    }
    $('#ap').prop('checked', config.network.ap_fallback);
    $('#ip').val(config.network.ip[0] + '.' +
            config.network.ip[1] + '.' +
            config.network.ip[2] + '.' +
            config.network.ip[3]);
    $('#netmask').val(config.network.netmask[0] + '.' +
            config.network.netmask[1] + '.' +
            config.network.netmask[2] + '.' +
            config.network.netmask[3]);
    $('#gateway').val(config.network.gateway[0] + '.' +
            config.network.gateway[1] + '.' +
            config.network.gateway[2] + '.' +
            config.network.gateway[3]);

    // E1.31 Config
    $('#universe').val(config.e131.universe);
    $('#universe_limit').val(config.e131.universe_limit);
    $('#channel_start').val(config.e131.channel_start);
    $('#multicast').prop('checked', config.e131.multicast);

    // Output Config
    $('.odiv').addClass('hidden');

    if (config.device.mode == 0x02) {  // WNRF
        mode = 'wnrf';
        $('#nrf_legacy').prop('checked', config.wnrf.enabled);
        $('#nrf_chan').val(config.wnrf.nrf_chan);
        $('#nrf_baud').val(config.wnrf.nrf_baud);
        if (config.wnrf.nrf_fw.length>0)
           $('#nrf_fw').text(config.wnrf.nrf_fw);
        else
           $('#nrf_fw').text('none');

        if ($('#nrf_legacy').is(':checked')) {
            $('.nrf').addClass('hidden');
         } else {
            $('.nrf').removeClass('hidden');
         }
    }
}

function getConfigStatus(data) {
    var status = JSON.parse(data);

    $('#x_ssid').text(status.ssid);
    $('#x_hostname').text(status.hostname);
    $('#x_ip').text(status.ip);
    $('#x_mac').text(status.mac);
    $('#x_version').text(status.version);
    $('#x_built').text(status.built);
    $('#x_flashchipid').text(status.flashchipid);
    $('#x_usedflashsize').text(status.usedflashsize);
    $('#x_realflashsize').text(status.realflashsize);
    $('#x_freeheap').text(status.freeheap);

}

function getEffectInfo(data) {
    parsed = JSON.parse(data);

    effectInfo = parsed.effectList;	// global effectInfo
    var running = parsed.currentEffect;

//  console.log (effectInfo);
//  console.log (effectInfo.t_chase);

    // process the effect configuration options
    $('#tmode').empty(); // clear the dropdown first
    for (var i in effectInfo) {
        var htmlid = effectInfo[i].htmlid;
        var name =   effectInfo[i].name;
        $('#tmode').append('<option value="' + htmlid + '">' + name + '</option>');
        if ( ! name.localeCompare(running.name) ) {
            $('#tmode').val(htmlid);
            hideShowTestSections();
        }
    }

    // set html based on current running effect
    $('.color').val('rgb(' + running.r + ',' + running.g + ',' + running.b + ')');
    $('.color').css('background-color', 'rgb(' + running.r + ',' + running.g + ',' + running.b + ')');
    $('#t_reverse').prop('checked', running.reverse);
    $('#t_mirror').prop('checked', running.mirror);
    $('#t_allleds').prop('checked', running.allleds);
    $('#t_speed').val(running.speed);
    $('#t_brightness').val(running.brightness);
    $('#t_startenabled').prop('checked', running.startenabled);
    $('#t_idleenabled').prop('checked', running.idleenabled);
    $('#t_idletimeout').val(running.idletimeout);

}

function getJsonStatus(data) {
    var status = JSON.parse(data);

    var rssi = +status.system.rssi;
    var quality = 2 * (rssi + 100);

    if (rssi <= -100)
        quality = 0;
    else if (rssi >= -50)
        quality = 100;

    $('#x_rssi').text(rssi);
    $('#x_quality').text(quality);

// getHeap(data)
    $('#x_freeheap').text( status.system.freeheap );

// getUptime
    var date = new Date(+status.system.uptime);
    var str = '';

    str += Math.floor(date.getTime()/86400000) + " days, ";
    str += ("0" + date.getUTCHours()).slice(-2) + ":";
    str += ("0" + date.getUTCMinutes()).slice(-2) + ":";
    str += ("0" + date.getUTCSeconds()).slice(-2);
    $('#x_uptime').text(str);

// getE131Status(data)
    $('#uni_first').text(status.e131.universe);
    $('#uni_last').text(status.e131.uniLast);
    $('#pkts').text(status.e131.num_packets);
    $('#serr').text(status.e131.seq_errors);
    $('#perr').text(status.e131.packet_errors);
    $('#clientip').text(status.e131.last_clientIP);

// getNrfStatus
    $('#stat_chan').text(status.nrf.chan);
    $('#stat_rate').text(status.nrf.baud);
    $('#stat_mode').text(status.nrf.mode);
}

function footermsg(message) {
    // Show footer msg
    var x = document.getElementById('footer');
    var bg = x.style.background;
    var fg = x.style.color;
    var msg = $('#name').text();


    x.style.background='rgb(255,48,48)';
    x.style.color="white";
    $('#name').text(message);

    setTimeout(function(){
        $('#name').text(msg);
        //x.style.background='rgb(14,14,14)';
        x.style.background=bg;
        x.style.color=fg ;
    }, 3000);
}

function snackmsg(message) {
    // Show snackbar for 3sec
    var x = document.getElementById('snackbar');
    x.innerHTML=message;
    x.className = 'show';
    setTimeout(function(){
        x.className = x.className.replace('show', '');
    }, 3000);
}

function setConfig() {
    // Get config to refresh UI and show result
    wsEnqueue("G1");
    snackmsg('Configuration Saved');
}

function submitWiFi() {
    var ip = $('#ip').val().split('.');
    var netmask = $('#netmask').val().split('.');
    var gateway = $('#gateway').val().split('.');

    var json = {
            'network': {
                'ssid': $('#ssid').val(),
                'passphrase': $('#password').val(),
                'hostname': $('#hostname').val(),
                'sta_timeout': parseInt($('#staTimeout').val()),
                'ip': [parseInt(ip[0]), parseInt(ip[1]), parseInt(ip[2]), parseInt(ip[3])],
                'netmask': [parseInt(netmask[0]), parseInt(netmask[1]), parseInt(netmask[2]), parseInt(netmask[3])],
                'gateway': [parseInt(gateway[0]), parseInt(gateway[1]), parseInt(gateway[2]), parseInt(gateway[3])],
                'dhcp': $('#dhcp').prop('checked'),
                'ap_fallback': $('#ap').prop('checked')
            }
        };
    wsEnqueue('S1' + JSON.stringify(json));
}

function submitConfig() {
    var channels = parseInt($('#s_count').val());

    var json = {
            'device': {
                'id': $('#devid').val()
            },
            'e131': {
                'universe': parseInt($('#universe').val()),
                'universe_limit': parseInt($('#universe_limit').val()),
                'channel_start': parseInt($('#channel_start').val()),
                'channel_count': channels,
                'multicast': $('#multicast').prop('checked')
            },
            'wnrf': {
                'nrf_chan': parseInt($('#nrf_chan').val()),
                'nrf_baud': parseInt($('#nrf_baud').val()),
                'enabled' : $('#nrf_legacy').prop('checked')
            }
    };

    wsEnqueue('S2' + JSON.stringify(json));
}

function submitStartupEffect() {
// not pretty - get current r,g,b from color picker
    var temp = $('.color').val().split(/\D+/);

    var currentEffectName = effectInfo[ $('#tmode option:selected').val() ].name;
//console.log (currentEffectName);

    var json = {
            'effects': {
                'name': currentEffectName,
                'mirror': $('#t_mirror').prop('checked'),
                'allleds': $('#t_allleds').prop('checked'),
                'reverse': $('#t_reverse').prop('checked'),
                'speed': parseInt($('#t_speed').val()),
                'r': temp[1],
                'g': temp[2],
                'b': temp[3],
                'brightness': parseFloat($('#t_brightness').val()),
                'startenabled': $('#t_startenabled').prop('checked'),
                'idleenabled': $('#t_idleenabled').prop('checked'),
                'idletimeout': parseInt($('#t_idletimeout').val())

            }
        };

    wsEnqueue('S3' + JSON.stringify(json));
}

function hideShowTestSections() {
    // Test mode toggles
//    $('.tdiv').addClass('hidden');
//    $('#'+$('select[name=tmode]').val()).removeClass('hidden');

    var tmode = $('#tmode option:selected').val();
//console.log('tmode is: ' + tmode);
    if ( (typeof tmode !== 'undefined') && (typeof effectInfo[tmode].wsTCode !== 'undefined') ) {
// hide/show view stream and testing options+startup
        if (effectInfo[tmode].hasColor) {
            $('#lab_color').removeClass('hidden');
            $('#div_color').removeClass('hidden');
	} else {
            $('#lab_color').addClass('hidden');
            $('#div_color').addClass('hidden');
        }
        if (effectInfo[tmode].hasMirror) {
            $('#div_mirror').removeClass('hidden');
	} else {
            $('#div_mirror').addClass('hidden');
        }
        if (effectInfo[tmode].hasReverse) {
            $('#div_reverse').removeClass('hidden');
	} else {
            $('#div_reverse').addClass('hidden');
        }
        if (effectInfo[tmode].hasAllLeds) {
            $('#div_allleds').removeClass('hidden');
	} else {
            $('#div_allleds').addClass('hidden');
        }
    }
}

// effect selector changed
function effectChanged() {
    hideShowTestSections();

    var tmode = $('#tmode option:selected').val();

//console.log ('found tcode ' + effectInfo[tmode].wsTCode);
    if (typeof effectInfo[tmode].wsTCode !== 'undefined') {
        wsEnqueue( effectInfo[tmode].wsTCode );
    }
}

function showReboot() {
    $('#update').modal('hide');
    $('#reboot').modal();
    setTimeout(function() {
        if($('#dhcp').prop('checked')) {
            window.location.assign("/");
        } else {
            window.location.assign("http://" + $('#ip').val());
        }
    }, 5000);
}

function reboot() {
    showReboot();
    wsEnqueue('X6');
}

//function getKeyByValue(object, value) {
//    return Object.keys(object).find(key => object[key] === value);
//}

function getKeyByValue(obj, value) {
    return Object.keys(obj)[Object.values(obj).indexOf(value)];
}

