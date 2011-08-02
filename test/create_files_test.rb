#!/usr/bin/env ruby
require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class CreateFilesTest < Verm::TestCase
  def test_saves_files_under_requested_path_and_hash
    post_file :path => '/foo',
              :file => 'fixtures/simple_text_file',
              :type => 'application/octet-stream',
              :expected_extension => nil
  end

  def test_saves_binary_files_without_truncation_or_miscoding
    post_file :path => '/foo',
              :file => 'fixtures/binary_file',
              :type => 'application/octet-stream',
              :expected_extension => nil
  end

  def test_saves_text_plain_files_with_suitable_extension
    post_file :path => '/foo',
              :file => 'fixtures/simple_text_file',
              :type => 'text/plain',
              :expected_extension => 'txt'
  end

  def test_saves_text_html_files_with_suitable_extension
    post_file :path => '/foo',
              :file => 'fixtures/simple_text_file',
              :type => 'text/html',
              :expected_extension => 'html'
  end
end
