
-- SUMMARY --

A generic Mollom client PHP class.

To submit bug reports and feature suggestions, or to track changes:
  http://drupal.org/project/issues/mollom


-- REQUIREMENTS --

* PHP 5


-- USAGE --

* Implement the Mollom class for your platform by extending it into a
  MollomMyPlatform class.

* For example, to check a post for spam:

    // When a comment is submitted:
    $mollom = new MollomMyPlatform();
    $result = $mollom->checkContent(array(
      'checks' => array('spam'),
      'postTitle' => $_POST['comment']['title'],
      'postBody' => $_POST['comment']['body'],
      'authorName' => $_POST['comment']['name'],
      'authorUrl' => $_POST['comment']['homepage'],
      'authorIp' => $_SERVER['REMOTE_ADDR'],
      'authorId' => $userid, // If you have one.
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
        print '<img src="' . $captcha['url'] . '" alt="Type the characters you see in this picture." />';
        print '<input type="text" name="captcha" size="10" value="" autocomplete="off" />';
        break;

      default:
        // If we end up here, Mollom responded with a unknown spamClassification.
        // Normally, this should not happen.
        break;
    }


* The full API documentation is available on http://mollom.com/api/rest


-- LICENSE --

The Mollom class is released under the GNU General Public License, Version 2.
See LICENSE.txt.

