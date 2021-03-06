--TEST--
Throwing exceptions on exit/die calls (with custom type)
--SKIPIF--
<?php if (!extension_loaded("DeepTrace")) print "Extension not loaded"; ?>
--FILE--
<?php
function exit_handler($msg = null) { echo 'Message: ' . $msg . "\n"; }
dt_set_exit_handler('exit_handler');
dt_throw_exit_exception(true, 'DeepTraceCustomExitException');
try {
	exit('DeepTrace Exit handler exception throwing test');
} catch (DeepTraceCustomExitException $e) {
	echo 'Catched exit-call: ' . $e->getMessage();
}
?>
--EXPECT--
Message: DeepTrace Exit handler exception throwing test
Catched exit-call: DeepTrace Exit handler exception throwing test