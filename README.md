A generic Mollom client PHP class.

This base class aims to ease integration of the [Mollom](http://mollom.com) content moderation service into PHP based applications.  The class implements essential code logic expected from Mollom clients and provides many helper functions to communicate with Mollom's [REST API](http://mollom.com/api/rest).

To submit bug reports and feature suggestions, or to track changes:
  https://github.com/Mollom/MollomPHP/issues


## Requirements

* [PHP](http://php.net) 5.2.4 or later


## Usage

* Extend the Mollom class for your platform.  At minimum, you need to implement the following methods:

    ```php
    <?php
    class MollomMyPlatform extends Mollom {

      public function loadConfiguration($name) {}
      
      public function saveConfiguration($name, $value) {}
      
      public function deleteConfiguration($name) {}
      
      public function getClientInformation() {}
      
      protected function request($method, $server, $path, $query = NULL, array $headers = array()) {}
    }
    ```

    These methods are documented in detail in the Mollom class.

* For example, to check a post for spam:

    ```php
    <?php
    // When a comment is submitted:
    $mollom = new MollomMyPlatform();
    $comment = $_POST['comment'];

    $result = $mollom->checkContent(array(
      'checks' => array('spam'),
      'postTitle' => $comment['title'],
      'postBody' => $comment['body'],
      'authorName' => $comment['name'],
      'authorUrl' => $comment['homepage'],
      'authorIp' => $_SERVER['REMOTE_ADDR'],
      'authorId' => $userid, // If the author is logged in.
    ));
    
    // You might want to make the fallback case configurable:
    if (!is_array($result) || !isset($result['id'])) {
      print "The content moderation system is currently unavailable. Please try again later.";
      die();
    }
    
    // Check the final spam classification.
    switch ($result['spamClassification']) {
      case 'ham':
        // Do nothing. (Accept content.)
        break;
    
      case 'spam':
        // Discard (block) the form submission.
        print "Your submission has triggered the spam filter and will not be accepted.";
        die();
        break;
    
      case 'unsure':
        // Require to solve a CAPTCHA to get the post submitted.
        $captcha = $mollom->createCaptcha(array(
          'contentId' => $result['id'],
          'type' => 'image',
        ));
        if (!is_array($captcha) || !isset($captcha['id'])) {
          print "The content moderation system is currently unavailable. Please try again later.";
          die();
        }
        // Output the CAPTCHA.
        print '<img src="' . $captcha['url'] . '" alt="Type the characters you see in this picture." />';
        print '<input type="text" name="captcha" size="10" value="" autocomplete="off" />';
        
        // Re-inject the submitted form values, re-render the form,
        // and ask the user to solve the CAPTCHA.
        break;
    
      default:
        // If we end up here, Mollom responded with a unknown spamClassification.
        // Normally, this should not happen.
        break;
    }
    ```


## Examples

These are examples for implementations of the Mollom class.  As visible in the above code snippet, the class only takes over the basic logic to communicate with Mollom.  Every implementation still needs application-specific code to handle the results provided by Mollom.

* [MollomDrupal](http://drupalcode.org/project/mollom.git/blob/refs/heads/7.x-2.x:/mollom.drupal.inc)
* [MollomWordpress](https://github.com/netsensei/WP-Mollom/blob/master/includes/mollom.wordpress.inc)

## License

You may use this software under the terms of either the MIT License or the
GNU General Public License (GPL), Version 2.

See LICENSE-MIT.txt and LICENSE-GPL.txt.

